"""
09_test_flexspi_stress.py — стресс-тест чтения QSPI через FlexSPI (PLAN.md, фаза 0.2–0.3).

Прошивка: tests/target/hil_flexspi_stress (RAM, pyOCD). Вопрос G0: на V3 к DQS-паду
FlexSPI (SD_B1_05) подключена висящая цепь LCD_DCDC_G. FCB читает флеш со стробом
через этот пад (RXCLKSRC=1, 133 МГц). Сравниваем старую плату и V3.

Предусловие: во флеш нет образа, который сам поднимает FlexSPI до загрузки по SWD
(удобнее всего — стёртая флеш, как для 08). Тест пишет PRNG-паттерн в последний
1 МБ флеш (0x60F00000) — на dev-плате с ФС ассетов это затрёт её хвост.

Наборы:
  1. prepare  — на 30 МГц записать паттерн (пропускается, если он уже на месте).
  2. matrix   — RXCLKSRC {1 — пад, 0 — внутренняя петля} × частота: чтение через AHB
                с кэшем и без и через IP-команды. Утверждения — только для режимов
                в пределах даташита (IMXRT1050IEC табл. 37/38: RXCLKSRC=0 ≤ 60 МГц,
                RXCLKSRC=1 ≤ 133 МГц); остальное — данные о запасе.
  3. dll      — окно выборки: DLLACR.OVRDVAL 0…63 (задержка выборки, ячейка ~75–225 пс,
                RM 27.5.14.4). Сколько ячеек проходит — запас по моменту выборки.
                Сравнение плат по ширине окна.
  4. soak     — долгое чтение в production-режиме (RXCLKSRC=1, 133 МГц), только если
                задан HIL_FLEXSPI_SOAK_S > 0.

Запуск:
  HIL_BOARD=legacy just host::hil-flexspi-stress
  HIL_BOARD=v3 just host::hil-flexspi-stress -k "matrix or dll"

Отчёт: $HIL_BUILD_DIR/hil_flexspi_stress_report_<HIL_BOARD>.json + таблица в выводе (-s).
"""

from __future__ import annotations

import json
import logging
import os
import time
from pathlib import Path

import pytest
import serial

import env_config as cfg
from pyocd_utils import flexram_init, load_elf, open_target, run_from_vectors

log = logging.getLogger(__name__)

pytestmark = [pytest.mark.flexspi_stress, pytest.mark.slow, pytest.mark.m5]

ELF = Path(cfg.BUILD_DIR) / "tests/target/hil_flexspi_stress/test_hil_flexspi_stress.elf"
BOARD = os.environ.get("HIL_BOARD", "unknown")  # метка в отчёте: v3 | legacy
REPORT = Path(cfg.BUILD_DIR) / f"hil_flexspi_stress_report_{BOARD}.json"

SEED = 0x51A7
REGION_MB = 1
POWER_OFF_S = 3.0
LOAD_RETRY_S = 15.0
RUN_TIMEOUT_S = 900.0
SILENCE_S = 5.0
JEDEC_W25Q128 = 0xEF4018

# (src, МГц): src 1 — строб через DQS-пад (как FCB), 0 — внутренняя петля
MATRIX = [(1, 133), (1, 120), (1, 99), (1, 60), (0, 30), (0, 60), (0, 80), (0, 99), (0, 120), (0, 133)]
IN_SPEC = {(1, 133), (1, 120), (1, 99), (1, 60), (0, 30), (0, 60)}
AHB_PASSES_CACHED = 50  # 50 МБ
AHB_PASSES_UNCACHED = 5
IP_PASSES = 10

DLL_SWEEP = [(1, 133), (0, 60), (0, 99)]
DLL_MAX = 63
DLL_PASSES = 3

SOAK_S = float(os.environ.get("HIL_FLEXSPI_SOAK_S", "0"))
SOAK_CHUNK_PASSES = 200

RESULTS: list[dict] = []
DLL_WINDOWS: list[dict] = []


class Qspi:
    def __init__(self, ser: serial.Serial, m5) -> None:
        self.ser = ser
        self.m5 = m5

    def _readline(self) -> str:
        return self.ser.readline().decode("ascii", errors="replace").strip()

    def boot(self) -> None:
        """Снять VIN → загрузить прошивку по SWD → дождаться READY."""
        assert ELF.exists(), f"ELF не найден: {ELF} (just build::build-hil)"
        self.m5.power(False)
        time.sleep(POWER_OFF_S)
        self.m5.power(True)
        time.sleep(cfg.POWER_ON_SETTLE_S)
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
        pytest.fail("hil_flexspi_stress не прислала READY")

    def cmd(self, line: str, timeout: float = 3.0) -> str:
        self.ser.write((line + "\r\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self._readline()
            if text and text not in ("READY", "P"):
                return text
        raise TimeoutError(f"нет ответа на '{line}'")

    def long(self, line: str, prefix: str) -> str:
        """Долгая команда: ждать строку с prefix; keepalive «P» — признак жизни."""
        self.ser.write((line + "\r\n").encode("ascii"))
        start = last_rx = time.monotonic()
        old_timeout, self.ser.timeout = self.ser.timeout, 0.2
        try:
            while time.monotonic() - start < RUN_TIMEOUT_S:
                text = self._readline()
                if text:
                    last_rx = time.monotonic()
                if text.startswith(prefix):
                    return text
                if time.monotonic() - last_rx > SILENCE_S:
                    pytest.fail(f"{line}: плата замолчала (сброс/зависание)")
        finally:
            self.ser.timeout = old_timeout
        pytest.fail(f"{line}: нет ответа за {RUN_TIMEOUT_S:.0f} с")

    @staticmethod
    def fields(line: str) -> dict[str, str]:
        return dict(kv.split("=", 1) for kv in line.split() if "=" in kv)

    def configure(self, src: int, mhz: int, dll: int = 0) -> dict[str, str]:
        resp = self.cmd(f"CFG {src} {mhz} {dll}")
        assert resp.startswith("CFG OK"), f"CFG {src} {mhz} {dll}: {resp}"
        return self.fields(resp)

    def run(self, test: str, passes: int) -> dict[str, str]:
        text = self.long(f"RUN {test} {passes} {SEED}", "RESULT")
        assert "ERR" not in text.split()[1], text
        return self.fields(text)


def record(suite: str, src: int, mhz: int, test: str, res: dict[str, str], **extra) -> int:
    errors = int(res["errors"])
    ms = int(res.get("ms", 0))
    passes = int(res.get("passes", 1))
    row = {"suite": suite, "src": src, "mhz": mhz, "test": test, "cache": res.get("cache"),
           "passes": passes, "errors": errors, "io": res.get("io"), "first": res.get("first"),
           "diff": res.get("diff"), "ms": ms,
           "mb_s": round(passes * REGION_MB * 1000 / ms, 1) if ms else None, **extra}
    RESULTS.append(row)
    log.info("%s src=%d %d МГц %s: errors=%d io=%s", suite, src, mhz, test, errors, res.get("io"))
    return errors


@pytest.fixture(scope="module")
def qspi(m5) -> Qspi:
    ser = serial.Serial(port=cfg.VCOM_PORT, baudrate=cfg.VCOM_BAUD, timeout=2.0, write_timeout=1.0)
    q = Qspi(ser, m5)
    q.boot()
    yield q
    ser.close()
    _write_report()


def _write_report() -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps({"board": BOARD, "results": RESULTS, "dll_windows": DLL_WINDOWS},
                                 indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n=== FlexSPI stress ({BOARD}): отчёт {REPORT} ===")
    for r in RESULTS:
        mark = "OK " if r["errors"] == 0 else "ERR"
        dll = f" dll={r['dll']}" if "dll" in r else ""
        print(f"  {mark} {r['suite']:7} src={r['src']} {r['mhz']:>3} МГц {r['test']:4} cache={r['cache']} "
              f"passes={r['passes']:>4} errors={r['errors']:>9} io={r['io']} {r['mb_s']} МБ/с{dll}")
    for w in DLL_WINDOWS:
        print(f"  DLL src={w['src']} {w['mhz']:>3} МГц: проходят OVRDVAL {w['pass_ranges']} "
              f"(из 0…{DLL_MAX})")


# ---------------------------------------------------------------------------
# 1. Паттерн во флеш
# ---------------------------------------------------------------------------


def test_prepare(qspi: Qspi):
    regs = qspi.configure(0, 30)
    assert int(regs["id"], 16) == JEDEC_W25Q128, f"JEDEC ID {regs['id']}, ожидался W25Q128 (0xEF4018)"
    resp = qspi.long(f"PREP {SEED}", "PREP")
    assert resp.startswith("PREP OK"), resp
    log.info("prepare: %s", resp)


# ---------------------------------------------------------------------------
# 2. Матрица режимов
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(("src", "mhz"), MATRIX, ids=[f"src{s}-{m}mhz" for s, m in MATRIX])
def test_matrix(qspi: Qspi, src: int, mhz: int):
    regs = qspi.configure(src, mhz)
    id_ok = int(regs["id"], 16) == JEDEC_W25Q128
    total = 0
    assert qspi.cmd("CACHE ON") == "OK"
    total += record("matrix", src, mhz, "AHB", qspi.run("AHB", AHB_PASSES_CACHED), root_khz=regs["root_khz"])
    assert qspi.cmd("CACHE OFF") == "OK"
    total += record("matrix", src, mhz, "AHB", qspi.run("AHB", AHB_PASSES_UNCACHED), root_khz=regs["root_khz"])
    assert qspi.cmd("CACHE ON") == "OK"
    total += record("matrix", src, mhz, "IP", qspi.run("IP", IP_PASSES), root_khz=regs["root_khz"])
    if (src, mhz) in IN_SPEC:
        assert id_ok, f"src={src} {mhz} МГц: JEDEC ID {regs['id']}"
        assert total == 0, f"src={src} {mhz} МГц (в пределах даташита): {total} ошибок"


# ---------------------------------------------------------------------------
# 3. Окно выборки по DLL
# ---------------------------------------------------------------------------


def _ranges(values: list[int]) -> str:
    if not values:
        return "нет"
    out, start, prev = [], values[0], values[0]
    for v in values[1:] + [None]:
        if v is not None and v == prev + 1:
            prev = v
            continue
        out.append(f"{start}…{prev}" if start != prev else f"{start}")
        if v is not None:
            start = prev = v
    return ", ".join(out)


@pytest.mark.parametrize(("src", "mhz"), DLL_SWEEP, ids=[f"src{s}-{m}mhz" for s, m in DLL_SWEEP])
def test_dll(qspi: Qspi, src: int, mhz: int):
    assert qspi.cmd("CACHE ON") == "OK"
    passing = []
    for dll in range(DLL_MAX + 1):
        qspi.configure(src, mhz, dll)
        errors = record("dll", src, mhz, "AHB", qspi.run("AHB", DLL_PASSES), dll=dll)
        if errors == 0:
            passing.append(dll)
    DLL_WINDOWS.append({"src": src, "mhz": mhz, "passing": passing, "pass_ranges": _ranges(passing)})
    log.info("DLL src=%d %d МГц: проходят %s", src, mhz, _ranges(passing))
    qspi.configure(1, 133)  # вернуть production-режим
    if (src, mhz) in IN_SPEC:
        assert 0 in passing, f"src={src} {mhz} МГц: не проходит даже OVRDVAL=0 (штатный DLLCR=0x100)"


# ---------------------------------------------------------------------------
# 4. Долгое чтение в production-режиме
# ---------------------------------------------------------------------------


@pytest.mark.skipif(SOAK_S <= 0, reason="HIL_FLEXSPI_SOAK_S не задан")
def test_soak(qspi: Qspi):
    qspi.configure(1, 133)
    assert qspi.cmd("CACHE ON") == "OK"
    deadline = time.monotonic() + SOAK_S
    total = 0
    while time.monotonic() < deadline:
        total += record("soak", 1, 133, "AHB", qspi.run("AHB", SOAK_CHUNK_PASSES))
    assert total == 0, f"soak {SOAK_S:.0f} с: {total} ошибок"
