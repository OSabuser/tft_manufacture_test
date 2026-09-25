"""
08_test_sdram_stress.py — стресс-тест SDRAM по DCD-вариантам (PLAN.md, фаза 0.2–0.3).

Прошивка: tests/target/hil_sdram_stress (RAM, pyOCD). Варианты DCD —
tests/target/hil_sdram_stress/dcd/*.txt (см. README там же).

Перед КАЖДЫМ вариантом — снятие VIN через M5 (холодный старт SEMC и SDRAM).
Холодность проверяется: BOOT до INIT должен показать SDRAMCR3.REN=0, т.е. никто
(DCD из флеш, smoke-test bootloader) SEMC не поднимал.

Предусловие: флеш стёрта (just host::flash-swd-erase). В прошитом образе с DCD
(firmware_test, tft_app) SEMC поднимает BootROM, bootloader поднимает сам —
холодного старта не будет, тест это обнаружит и упадёт с подсказкой.
На плате V3 при стёртой флеш BootROM не кормит аппаратный сторож; прошивка
загружается сразу после подачи питания и дальше кормит сама.

Наборы:
  1. functional   — все варианты: DATABUS, ADDRBUS, MARCH ×2 (без кэша),
                    PRNG без кэша и с кэшем (burst-ы BL8).
  2. in1          — влияние оптовхода EXT_IN1 (на V3 = DQS-пад SEMC):
                    candidate vs candidate_dqsmd0 × IN1 {выкл, вкл, меандр}.
                    Сначала сверка проводки (test_in1_wiring): прошивка читает пин EXT_IN1
                    (старая плата — AD_B1_06, V3 — EMC_39 в варианте с DQSMD=0) и считает
                    фронты меандра. Без подтверждённой проводки in1 падает. Нужен HIL_BOARD.
  3. retention    — удержание 1/5/30 с без обращений: legacy, sdram_c,
                    candidate, candidate_no_refresh (контроль запаса ячеек).

Утверждения — только для конфигураций, которые должны работать (legacy, sdram_c,
candidate); остальные варианты — данные для решения (G0), попадают в отчёт.

Запуск:
  just host::hil-sdram-stress
  just host::hil-sdram-stress -k functional

Отчёт: $HIL_BUILD_DIR/hil_sdram_stress_report_<HIL_BOARD>.json + таблица в выводе (-s).
"""

from __future__ import annotations

import json
import logging
import os
import sys
import threading
import time
from pathlib import Path

import pytest
import serial
from elftools.elf.elffile import ELFFile

import env_config as cfg
from pyocd_utils import flexram_init, load_elf, open_target, run_from_vectors

log = logging.getLogger(__name__)

# Долгий, требует стёртой флеш — из общего just host::hil-run исключён.
pytestmark = [pytest.mark.sdram_stress, pytest.mark.slow, pytest.mark.m5]

ELF = Path(cfg.BUILD_DIR) / "tests/target/hil_sdram_stress/test_hil_sdram_stress.elf"
BOARD = os.environ.get("HIL_BOARD", "unknown")  # метка в отчёте: v3 | legacy
REPORT = Path(cfg.BUILD_DIR) / f"hil_sdram_stress_report_{BOARD}.json"  # по плате — не затирать

VARIANTS = [
    "legacy", "sdram_c", "candidate", "candidate_no_refresh",
    "candidate_dqsmd0", "candidate_dqsmd0_132mhz", "candidate_164mhz",
    # запас внутренней петли по частоте (V3 2026-09-25: 148,5 чисто, 163,86 — DQ9/DQ15)
    "candidate_dqsmd0_148mhz", "candidate_dqsmd0_153mhz", "candidate_dqsmd0_158mhz",
    "candidate_dqsmd0_164mhz",
]
MUST_PASS = {"legacy", "sdram_c", "candidate"}

POWER_OFF_S = 3.0
LOAD_RETRY_S = 15.0
RUN_TIMEOUT_S = 900.0  # общий предел одного RUN
SILENCE_S = 5.0  # нет keepalive «P» дольше — плата умерла/сброшена
IN1_TOGGLE_HZ = 5.0
IN1_SETTLE_S = 0.1  # реле M5 + оптопара, как в 02_test_opto.py
IN1_EDGES_MS = 2000
IN1_PIN = {"legacy": "ad_b1_06", "v3": "emc_39"}  # где EXT_IN1 на ревизии платы
RETENTION_S = [1, 5, 30]

RESULTS: list[dict] = []

VARIANTS_DIR = Path(__file__).resolve().parents[2] / "tests/target/hil_sdram_stress/dcd"
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
import dcd_tool  # noqa: E402  (stdlib-only конвертер DCD)


def _elf_symbol(name: str) -> int:
    with open(ELF, "rb") as f:
        sym = ELFFile(f).get_section_by_name(".symtab").get_symbol_by_name(name)
    assert sym, f"символ {name} не найден в {ELF.name}"
    return sym[0]["st_value"]


def _describe_offset(variant: str, offset: int) -> str:
    """Какой элемент DCD лежит по смещению (пара write или заголовок check/NOP)."""
    cmds = dcd_tool.parse_txt((VARIANTS_DIR / f"{variant}.txt").read_text(encoding="utf-8"))
    pos = 4
    for cmd in cmds:
        if isinstance(cmd, dcd_tool.Write):
            for i, (addr, value) in enumerate(cmd.pairs):
                if pos + 4 + 8 * i == offset:
                    name = dcd_tool.WRITE_NAMES[cmd.flags]
                    return f"{name} 0x{addr:08X} <- 0x{value:08X}"
            pos += 4 + 8 * len(cmd.pairs)
        else:
            if pos == offset:
                return dcd_tool._describe(cmd)  # noqa: SLF001
            pos += 16 if isinstance(cmd, dcd_tool.Check) and cmd.count is not None else (
                12 if isinstance(cmd, dcd_tool.Check) else 4)
    return "смещение вне DCD"


def diagnose_hang(variant: str) -> str:
    """Плата не отвечает: остановить ядро, прочитать PC/отказы и где встал интерпретатор DCD."""
    lines = []
    try:
        with open_target(frequency=cfg.PYOCD_FREQUENCY) as target:
            pc, lr = target.read_core_register("pc"), target.read_core_register("lr")
            ipsr = target.read_core_register("xpsr") & 0x1FF
            cfsr, hfsr = target.read32(0xE000ED28), target.read32(0xE000ED2C)
            bfar = target.read32(0xE000ED38)
            lines.append(f"PC=0x{pc:08X} LR=0x{lr:08X} IPSR={ipsr} (3=HardFault, 5=BusFault)")
            lines.append(f"CFSR=0x{cfsr:08X} HFSR=0x{hfsr:08X} BFAR=0x{bfar:08X}")
            off = target.read32(_elf_symbol("g_bsp_sdram_dcd_offset"))
            where = "ни одной" if off == 0xFFFFFFFF else f"@{off}: {_describe_offset(variant, off)}"
            lines.append(f"DCD: последний начатый элемент — {where}")
    except Exception as exc:  # ядро может не остановиться, если шина встала намертво
        lines.append(f"диагностика не удалась: {exc}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Таргет
# ---------------------------------------------------------------------------


class Stress:
    def __init__(self, ser: serial.Serial, m5) -> None:
        self.ser = ser
        self.m5 = m5

    def _readline(self) -> str:
        return self.ser.readline().decode("ascii", errors="replace").strip()

    def load(self) -> None:
        assert ELF.exists(), f"ELF не найден: {ELF} (just build::build-hil)"
        deadline = time.monotonic() + LOAD_RETRY_S
        while True:
            try:
                with open_target(frequency=cfg.PYOCD_FREQUENCY) as target:
                    flexram_init(target)
                    load_elf(target, str(ELF))
                    run_from_vectors(target)
                break
            except Exception as exc:  # pyOCD: TransferError и т.п.
                if time.monotonic() > deadline:
                    pytest.fail(f"SWD: не удалось загрузить прошивку: {exc}")
                time.sleep(0.5)
        self.ser.reset_input_buffer()
        ready_deadline = time.monotonic() + cfg.READY_TIMEOUT
        while time.monotonic() < ready_deadline:
            if self._readline() == "READY":
                self.ser.reset_input_buffer()
                return
        pytest.fail("hil_sdram_stress не прислала READY")

    def cmd(self, line: str, timeout: float = 3.0) -> str:
        self.ser.write((line + "\r\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self._readline()
            if text and text not in ("READY", "P"):
                return text
        raise TimeoutError(f"нет ответа на '{line}'")

    @staticmethod
    def fields(line: str) -> dict[str, str]:
        return dict(kv.split("=", 1) for kv in line.split() if "=" in kv)

    def fresh(self, variant: str) -> dict[str, str]:
        """Снять VIN → загрузить → убедиться в холодном SEMC → INIT варианта."""
        self.m5.power(False)
        time.sleep(POWER_OFF_S)
        self.m5.power(True)
        time.sleep(cfg.POWER_ON_SETTLE_S)
        self.load()
        boot = self.fields(self.cmd("BOOT"))
        assert boot["ren"] == "0", (
            f"SEMC уже поднят до INIT (sdramcr3={boot['sdramcr3']}): во флеш образ с DCD или "
            "bootloader. Сотрите флеш: just host::flash-swd-erase"
        )
        try:
            resp = self.cmd(f"INIT {variant}", timeout=5.0)
        except TimeoutError:
            pytest.fail(f"INIT {variant}: плата не ответила. Диагностика:\n{diagnose_hang(variant)}")
        assert resp.startswith("INIT OK"), f"{variant}: {resp}"
        log.info("%s: %s", variant, resp)
        return self.fields(resp)

    def run(self, test: str) -> dict[str, str]:
        """RUN с ожиданием RESULT; keepalive «P» — признак жизни."""
        self.ser.write((f"RUN {test}" + "\r\n").encode("ascii"))
        start = last_rx = time.monotonic()
        old_timeout, self.ser.timeout = self.ser.timeout, 0.2
        try:
            while time.monotonic() - start < RUN_TIMEOUT_S:
                text = self._readline()
                if text:
                    last_rx = time.monotonic()
                if text.startswith("RESULT"):
                    assert "ERR" not in text.split()[1], f"RUN {test}: {text}"
                    return self.fields(text)
                if time.monotonic() - last_rx > SILENCE_S:
                    pytest.fail(f"RUN {test}: плата замолчала (сброс/зависание)")
        finally:
            self.ser.timeout = old_timeout
        pytest.fail(f"RUN {test}: нет RESULT за {RUN_TIMEOUT_S:.0f} с")


def record(suite: str, variant: str, test: str, res: dict[str, str], **extra) -> int:
    errors = int(res["errors"])
    row = {"suite": suite, "variant": variant, "test": test, "errors": errors,
           "cache": res.get("cache"), "dq": res.get("dq"), "first": res.get("first"),
           "diff": res.get("diff"), "ms": int(res.get("ms", 0)), **extra}
    RESULTS.append(row)
    log.info("%s %s %s: errors=%d dq=%s", suite, variant, test, errors, res.get("dq"))
    return errors


@pytest.fixture(scope="module")
def stress(m5) -> Stress:
    ser = serial.Serial(port=cfg.VCOM_PORT, baudrate=cfg.VCOM_BAUD, timeout=2.0, write_timeout=1.0)
    yield Stress(ser, m5)
    ser.close()
    _write_report()


def _write_report() -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps({"board": BOARD, "results": RESULTS}, indent=2,
                                 ensure_ascii=False), encoding="utf-8")
    print(f"\n=== SDRAM stress ({BOARD}): отчёт {REPORT} ===")
    for r in RESULTS:
        extra = " ".join(f"{k}={r[k]}" for k in ("in1", "in1_check", "seconds") if k in r)
        mark = "OK " if r["errors"] == 0 else "ERR"
        print(f"  {mark} {r['suite']:10} {r['variant']:24} {r['test']:22} cache={r['cache']} "
              f"errors={r['errors']:>8} dq={r['dq']} {extra}")


# ---------------------------------------------------------------------------
# 0. Прошивка знает все варианты
# ---------------------------------------------------------------------------


def test_variants_list(stress: Stress):
    stress.load()
    listed = stress.cmd("VARIANTS").split(" ", 1)[1].split(",")
    assert set(listed) == set(VARIANTS), f"в прошивке: {listed}"


# ---------------------------------------------------------------------------
# 1. Функциональный набор по всем вариантам
# ---------------------------------------------------------------------------

FUNCTIONAL = [
    ("OFF", "DATABUS"), ("OFF", "ADDRBUS"), ("OFF", "MARCH 00000000"),
    ("OFF", "MARCH 55555555"), ("OFF", "PRNG 1"), ("ON", "PRNG 2"), ("ON", "MARCH AAAAAAAA"),
]


@pytest.mark.parametrize("variant", VARIANTS)
def test_functional(stress: Stress, variant: str):
    regs = stress.fresh(variant)
    total = 0
    for cache, test in FUNCTIONAL:
        assert stress.cmd(f"CACHE {cache}") == "OK"
        total += record("functional", variant, test, stress.run(test),
                        semc_khz=regs.get("semc_khz"))
    if variant in MUST_PASS:
        assert total == 0, f"{variant}: {total} ошибок в функциональном наборе"


# ---------------------------------------------------------------------------
# 2. Влияние EXT_IN1 (на V3 — DQS-пад SEMC, EMC_39)
# ---------------------------------------------------------------------------


class _Toggler:
    def __init__(self, m5, hz: float) -> None:
        self.m5, self.half = m5, 0.5 / hz
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def _loop(self) -> None:
        state = False
        while not self._stop.is_set():
            state = not state
            self.m5.opto_set(1, state)
            time.sleep(self.half)

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        self._thread.join()
        self.m5.opto_set(1, False)


# Проводка подтверждена test_in1_wiring (None — не проверялась). Нужна для V3 + candidate:
# там EMC_39 занят SEMC_DQS и уровень на пине прошивке не прочитать.
_IN1_WIRED: bool | None = None


def _in1_pin() -> str:
    if BOARD not in IN1_PIN:
        pytest.fail(f"HIL_BOARD={BOARD}: для in1 задайте HIL_BOARD=legacy|v3 (от этого зависит пин EXT_IN1)")
    return IN1_PIN[BOARD]


def _in1_level(stress: Stress) -> str:
    resp = stress.cmd("IN1")
    assert resp.startswith("IN1 "), f"IN1: {resp}"
    return stress.fields(resp)[_in1_pin()]


def _in1_edges(stress: Stress) -> int:
    with _Toggler(stress.m5, IN1_TOGGLE_HZ):
        resp = stress.cmd(f"IN1 EDGES {IN1_EDGES_MS}", timeout=IN1_EDGES_MS / 1000 + 3.0)
    assert resp.startswith("IN1 EDGES"), f"IN1 EDGES: {resp}"
    return int(stress.fields(resp)[_in1_pin()])


def check_in1_wiring(stress: Stress, edges: bool) -> bool:
    """Выкл → 0, вкл → 1, выкл → 0 (и фронты меандра). False — пин в этом варианте не читается."""
    pin = _in1_pin()
    hint = (f"оптоканал 1 M5 не доходит до EXT_IN1 ({pin}, HIL_BOARD={BOARD}): "
            "проверьте подключение выхода M5 к входу IN1 платы")
    for state, expected in ((False, "0"), (True, "1"), (False, "0")):
        stress.m5.opto_set(1, state)
        time.sleep(IN1_SETTLE_S)
        level = _in1_level(stress)
        if level == "-":
            return False
        assert level == expected, f"{hint}; opto={'вкл' if state else 'выкл'} → {pin}={level}"
    if edges:
        n = _in1_edges(stress)
        expected_n = int(2 * IN1_TOGGLE_HZ * IN1_EDGES_MS / 1000)
        assert n >= expected_n // 2, f"{hint}; меандр {IN1_TOGGLE_HZ} Гц: {n} фронтов за " \
            f"{IN1_EDGES_MS} мс, ожидалось ≈{expected_n}"
        log.info("IN1 %s: уровни 0/1/0, меандр %d фронтов (≈%d)", pin, n, expected_n)
    return True


def test_in1_wiring(stress: Stress):
    """До набора in1: EXT_IN1 реально управляется M5 (иначе in1 ничего не доказывает)."""
    global _IN1_WIRED
    _IN1_WIRED = False
    stress.fresh("candidate_dqsmd0")  # на V3 EMC_39 — GPIO; на старой плате пин не зависит от DCD
    try:
        assert check_in1_wiring(stress, edges=True), "EXT_IN1 не читается в candidate_dqsmd0"
    finally:
        stress.m5.opto_set(1, False)
    _IN1_WIRED = True


@pytest.mark.parametrize("in1", ["off", "on", "toggle"])
@pytest.mark.parametrize("variant", ["candidate", "candidate_dqsmd0", "candidate_dqsmd0_164mhz"])
def test_in1(stress: Stress, variant: str, in1: str):
    stress.fresh(variant)
    try:
        wired = check_in1_wiring(stress, edges=in1 == "toggle")
    finally:
        stress.m5.opto_set(1, False)
    if not wired and _IN1_WIRED is not True:
        pytest.fail(f"{variant}: EXT_IN1 в этом варианте не читается (пад занят SEMC_DQS), а "
                    "проводка не подтверждена test_in1_wiring — запускайте набор целиком (-k in1)")
    check = "pin" if wired else "wiring_test"
    stress.m5.opto_set(1, in1 == "on")
    time.sleep(IN1_SETTLE_S)
    total = 0
    try:
        for cache, test in [("ON", "PRNG 3"), ("OFF", "MARCH 00000000")]:
            assert stress.cmd(f"CACHE {cache}") == "OK"
            if in1 == "toggle":
                with _Toggler(stress.m5, IN1_TOGGLE_HZ):
                    res = stress.run(test)
            else:
                res = stress.run(test)
            total += record("in1", variant, test, res, in1=in1, in1_check=check)
    finally:
        stress.m5.opto_set(1, False)
    if variant == "candidate" and in1 == "off":
        assert total == 0, "candidate без IN1 должен проходить"


# ---------------------------------------------------------------------------
# 3. Удержание без обращений
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("variant", ["legacy", "sdram_c", "candidate", "candidate_no_refresh"])
def test_retention(stress: Stress, variant: str):
    stress.fresh(variant)
    assert stress.cmd("CACHE OFF") == "OK"
    total = 0
    for seconds in RETENTION_S:
        total += record("retention", variant, f"RETENTION {seconds}",
                        stress.run(f"RETENTION {seconds} {seconds + 10}"), seconds=seconds)
    if variant in MUST_PASS:
        assert total == 0, f"{variant}: {total} ошибок удержания при комнатной температуре"
