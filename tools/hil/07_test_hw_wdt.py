"""
07_test_hw_wdt.py — измерение аппаратного watchdog платы V3.x (LED_BLNK → EN BL9362).

Цель — узнать, как кормить сторожа, до того как писать под новую плату остальное:
  • таймаут: сколько плата живёт, если LED_BLNK перестал переключаться
    (отдельно для «LED горит» и «LED не горит»);
  • максимальный период кормления, который сторож ещё принимает;
  • достаточно ли короткого импульса (сторож видит фронты через C73).

Стенд: питание таргета через M5 RLY1 = VIN (сторож работает ТОЛЬКО от VIN —
компаратор питается от 4V9, сброс идёт через EN основного DC/DC).
Прошивка: tests/target/hil_hw_wdt (RAM, грузится pyOCD).

Как меряем: после HOLD прошивка каждые 10 мс шлёт «T <мс>»; последняя
принятая метка перед тишиной = таймаут (точность ~10 мс).

Чем доказываем, что это именно сторож по питанию, а не что-то ещё:
  1. Прошивка при старте читает и ЧИСТИТ SRC_SRSR. Контроль метода: без сброса
     следующая загрузка видит SRSR=0; после снятия VIN через M5 — ровно POR.
  2. После каждого срабатывания SRSR = ровно POR (IPP_RESET_B), без флагов
     WDOG/WDOG3/lockup/user reset — т.е. пропадало питание чипа.
  3. При каждой загрузке встроенные сторожа (WDOG1/2, RTWDOG) выключены.
  4. Кормление держит плату втрое дольше измеренного таймаута — сброс вызывает
     отсутствие фронтов LED_BLNK, а не время.
  5. Таймаут стабилен (разброс ≤ 10 %), развёртка периода монотонна.

Предусловие: во флеш НЕ прошит bootloader (его bsp_boot_state_init() чистит
SRSR раньше, чем pyOCD загрузит тестовую прошивку). firmware_test или пустая
флеш подходят. Проверяется контрольным тестом.

Запуск:
  just host::hil-hw-wdt
  uv run pytest 07_test_hw_wdt.py -s -v

Отчёт: $HIL_BUILD_DIR/hil_hw_wdt_report.json + таблица в выводе (-s).
"""

from __future__ import annotations

import json
import logging
import os
import statistics
import time
from dataclasses import dataclass, field
from pathlib import Path

import pytest
import serial

import env_config as cfg
from pyocd_utils import flexram_init, load_elf, open_target, run_from_vectors

log = logging.getLogger(__name__)

# Только плата V3.x; из общего just host::hil-run исключено (на старой плате сторожа нет).
pytestmark = [pytest.mark.board_v3, pytest.mark.slow, pytest.mark.m5]

ELF = Path(cfg.BUILD_DIR) / "tests/target/hil_hw_wdt/test_hil_hw_wdt.elf"
REPORT = Path(cfg.BUILD_DIR) / "hil_hw_wdt_report.json"

MAX_WAIT_S = float(os.environ.get("HIL_HW_WDT_MAX_S", "60"))  # дольше — «сторож не сработал»
SILENCE_S = 1.0  # тишина в UART дольше этого = плата сброшена
LOAD_RETRY_S = float(os.environ.get("HIL_HW_WDT_LOAD_RETRY_S", "15"))  # плата поднимается после сброса
HOLD_REPEATS = int(os.environ.get("HIL_HW_WDT_REPEATS", "3"))
FEED_OBSERVE_FACTOR = 3.0  # кормим столько таймаутов, чтобы считать период рабочим
SPREAD_TOLERANCE = 0.10  # допустимый разброс таймаута
# Измерено 2026-09-25 (плата V3.1, 6 замеров 29 710…30 340 мс) — только для запуска подмножества
# тестов без HOLD (-k); 0 — не подставлять.
KNOWN_TIMEOUT_MS = int(os.environ.get("HIL_HW_WDT_KNOWN_T_MS", "30340"))

# SRC_SRSR (RM 21.8.3)
SRSR_POR = 0x001  # IPP_RESET_B — power-on reset
SRSR_NAMES = {
    0x001: "POR", 0x002: "LOCKUP/SYSRESETREQ", 0x004: "CSU", 0x008: "USER_RESET",
    0x010: "WDOG", 0x020: "JTAG_HIGHZ", 0x040: "JTAG_SW", 0x080: "WDOG3", 0x100: "TEMPSENSE",
}


def srsr_str(value: int) -> str:
    names = [n for bit, n in SRSR_NAMES.items() if value & bit]
    return f"0x{value:03X} ({'+'.join(names) or 'нет сброса'})"


@dataclass
class Boot:
    srsr: int
    wdog1: bool
    wdog2: bool
    rtwdog: bool

    @property
    def internal_wdog_on(self) -> bool:
        return self.wdog1 or self.wdog2 or self.rtwdog


@dataclass
class Results:
    timeouts_ms: dict[str, list[int]] = field(default_factory=lambda: {"ON": [], "OFF": []})
    reset_srsr: list[str] = field(default_factory=list)
    long_feed: dict = field(default_factory=dict)
    sweep: list[dict] = field(default_factory=list)
    short_pulse: dict = field(default_factory=dict)
    pulse_width: list[dict] = field(default_factory=list)
    patterns: list[dict] = field(default_factory=list)

    def all_timeouts(self) -> list[int]:
        values = self.timeouts_ms["ON"] + self.timeouts_ms["OFF"]
        if values:
            return values
        if KNOWN_TIMEOUT_MS:
            # Запуск подмножества (-k) без тестов HOLD — берём ранее измеренное значение.
            return [KNOWN_TIMEOUT_MS]
        pytest.skip("таймаут сторожа не измерен (упали тесты HOLD)")


RESULTS = Results()


# ---------------------------------------------------------------------------
# Таргет: загрузка, команды, наблюдение за потоком
# ---------------------------------------------------------------------------


class WdtBoard:
    def __init__(self, ser: serial.Serial) -> None:
        self.ser = ser

    def load(self) -> Boot:
        """Загрузить прошивку в RAM, дождаться READY, вернуть диагностику старта.

        Повторяет подключение до LOAD_RETRY_S: после срабатывания сторожа плата
        какое-то время обесточена/стартует, SWD в этот момент отвечает No ACK.
        """
        assert ELF.exists(), f"ELF не найден: {ELF} (just build::build-hil)"
        deadline = time.monotonic() + LOAD_RETRY_S
        while True:
            try:
                with open_target(frequency=cfg.PYOCD_FREQUENCY) as target:
                    flexram_init(target)
                    load_elf(target, str(ELF))
                    run_from_vectors(target)
                break
            except Exception as exc:  # pyOCD: TransferError, ProbeError и т.п.
                if time.monotonic() > deadline:
                    pytest.fail(f"SWD: не удалось загрузить прошивку за {LOAD_RETRY_S:.0f} с: {exc}")
                log.info("SWD недоступен (%s), повтор", exc)
                time.sleep(0.5)

        self.ser.reset_input_buffer()
        ready_deadline = time.monotonic() + cfg.READY_TIMEOUT
        while time.monotonic() < ready_deadline:
            if self._readline() == "READY":
                self.ser.reset_input_buffer()
                return self.boot()
        pytest.fail(f"hil_hw_wdt не прислала READY за {cfg.READY_TIMEOUT} с")

    def _readline(self) -> str:
        return self.ser.readline().decode("ascii", errors="replace").strip()

    def cmd(self, line: str) -> str:
        """Команда → первая строка ответа, пропуская поток «T …» и мусор."""
        self.ser.write((line + "\r\n").encode("ascii"))
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            text = self._readline()
            if text and not text.startswith("T ") and text != "READY":
                return text
        raise TimeoutError(f"нет ответа на '{line}'")

    def boot(self) -> Boot:
        resp = self.cmd("BOOT")
        assert resp.startswith("BOOT "), f"неожиданный ответ на BOOT: {resp!r}"
        fields = dict(kv.split("=", 1) for kv in resp.split()[1:])
        info = Boot(
            srsr=int(fields["srsr"], 16),
            wdog1=fields["wdog1"] == "1",
            wdog2=fields["wdog2"] == "1",
            rtwdog=fields["rtwdog"] == "1",
        )
        log.info("BOOT: srsr=%s wdog1=%d wdog2=%d rtwdog=%d", srsr_str(info.srsr),
                 info.wdog1, info.wdog2, info.rtwdog)
        return info

    def watch(self, duration_s: float) -> tuple[bool, int]:
        """Смотреть поток до тишины или duration_s. → (плата жива, последняя метка T мс).

        Битые строки (в момент пропадания питания UART выдаёт мусор) пропускаются.
        """
        last_t, last_rx = 0, time.monotonic()
        end = last_rx + duration_s
        old_timeout, self.ser.timeout = self.ser.timeout, 0.1
        try:
            while time.monotonic() < end:
                text = self._readline()
                if text.startswith("T ") and text[2:].isdigit():
                    last_t, last_rx = int(text[2:]), time.monotonic()
                elif time.monotonic() - last_rx > SILENCE_S:
                    return False, last_t
            return True, last_t
        finally:
            self.ser.timeout = old_timeout

    def expect_power_cycle(self, context: str) -> None:
        """После тишины: перезагрузить прошивку и убедиться, что это был ровно POR."""
        info = self.load()
        RESULTS.reset_srsr.append(f"{context}: {srsr_str(info.srsr)}")
        assert info.srsr == SRSR_POR, (
            f"{context}: ожидался только POR (пропадание питания), SRSR={srsr_str(info.srsr)}"
        )


@pytest.fixture(scope="module")
def board(m5) -> WdtBoard:
    ser = serial.Serial(port=cfg.VCOM_PORT, baudrate=cfg.VCOM_BAUD, timeout=2.0, write_timeout=1.0)
    brd = WdtBoard(ser)
    brd.load()  # первая загрузка чистит SRSR, дальше он отражает только новые сбросы
    yield brd
    ser.close()
    _write_report()


def _loaded_and_quiet(board: WdtBoard) -> Boot:
    """Загрузка перед измерением + строгие предусловия."""
    info = board.load()
    assert not info.internal_wdog_on, (
        f"встроенный сторож взведён (wdog1={info.wdog1} wdog2={info.wdog2} "
        f"rtwdog={info.rtwdog}) — сброс нельзя будет приписать LED_BLNK. "
        "Во флеш прошит bootloader? Сотрите флеш или прошейте firmware_test."
    )
    return info


def _write_report() -> None:
    data = {
        "timeouts_ms": RESULTS.timeouts_ms,
        "reset_causes": RESULTS.reset_srsr,
        "long_feed": RESULTS.long_feed,
        "feed_period_sweep": RESULTS.sweep,
        "short_pulse": RESULTS.short_pulse,
        "pulse_width_sweep": RESULTS.pulse_width,
        "real_patterns": RESULTS.patterns,
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n=== Аппаратный watchdog: отчёт {REPORT} ===")
    for level, values in RESULTS.timeouts_ms.items():
        print(f"  HOLD {level:3}: таймаут {values} мс")
    for line in RESULTS.reset_srsr:
        print(f"  сброс {line}")
    if RESULTS.long_feed:
        print(f"  длительное кормление {RESULTS.long_feed}")
    for row in RESULTS.sweep:
        print(f"  FEED период {row['period_ms']:6} мс: {'жива' if row['alive'] else 'СБРОС'}")
    if RESULTS.short_pulse:
        print(f"  импульс {RESULTS.short_pulse}")
    for row in RESULTS.pulse_width:
        print(f"  импульс {row['polarity']:4} {row['width_ms']:4} мс / {row['period_ms']} мс: "
              f"{'жива' if row['alive'] else 'СБРОС'}")
    for row in RESULTS.patterns:
        print(f"  паттерн {row['name']}: {'кормит' if row['alive'] else 'НЕ КОРМИТ'}")


# ---------------------------------------------------------------------------
# 1. Контроль метода
# ---------------------------------------------------------------------------


def test_control_reload_without_reset_sees_clean_srsr(board: WdtBoard):
    """Перезагрузка прошивки без сброса → SRSR=0 (прошлая загрузка его очистила)."""
    info = _loaded_and_quiet(board)
    assert info.srsr == 0, (
        f"SRSR={srsr_str(info.srsr)} без сброса — между загрузками что-то сбрасывает чип "
        "или прошитый образ пишет в SRSR"
    )


def test_control_vin_power_cycle_reports_por(board: WdtBoard, m5):
    """Снятие VIN через M5 → SRSR = ровно POR. Иначе метод не различает пропадание питания."""
    m5.power(False)
    time.sleep(3.0)
    m5.power(True)
    time.sleep(cfg.POWER_ON_SETTLE_S)
    info = board.load()
    assert info.srsr == SRSR_POR, (
        f"после снятия VIN SRSR={srsr_str(info.srsr)}, ожидался POR. Если 0 — прошитый "
        "образ чистит SRSR (bootloader): сотрите флеш или прошейте firmware_test."
    )


# ---------------------------------------------------------------------------
# 2. Таймаут: перестаём кормить, держим уровень
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("repeat", range(HOLD_REPEATS))
@pytest.mark.parametrize("level", ["ON", "OFF"])
def test_hold_triggers_power_cycle(board: WdtBoard, level: str, repeat: int):
    _loaded_and_quiet(board)
    assert board.cmd(f"HOLD {level}") == "OK"
    alive, last_t = board.watch(MAX_WAIT_S)
    assert not alive, (
        f"сторож не сработал за {MAX_WAIT_S:.0f} с при HOLD {level}: не запаян, "
        "или таргет питается не от VIN (M5 RLY1)?"
    )
    board.expect_power_cycle(f"HOLD {level} #{repeat}")
    RESULTS.timeouts_ms[level].append(last_t)
    log.info("HOLD %s #%d: таймаут %d мс", level, repeat, last_t)


def test_timeout_is_stable_and_level_independent():
    values = RESULTS.all_timeouts()
    median = statistics.median(values)
    spread = (max(values) - min(values)) / median
    assert spread <= SPREAD_TOLERANCE, f"разброс таймаута {spread:.0%}: {values}"
    on, off = RESULTS.timeouts_ms["ON"], RESULTS.timeouts_ms["OFF"]
    if on and off:
        diff = abs(statistics.median(on) - statistics.median(off)) / median
        assert diff <= SPREAD_TOLERANCE, f"таймаут зависит от уровня: ON {on}, OFF {off}"


# ---------------------------------------------------------------------------
# 3. Кормление держит плату дольше таймаута — сброс вызывают именно фронты
# ---------------------------------------------------------------------------


def test_feeding_keeps_board_alive_beyond_timeout(board: WdtBoard):
    duration_s = FEED_OBSERVE_FACTOR * max(RESULTS.all_timeouts()) / 1000.0
    _loaded_and_quiet(board)
    assert board.cmd("FEED 200 100") == "OK"
    alive, last_t = board.watch(duration_s)
    RESULTS.long_feed = {"period_ms": 200, "on_ms": 100, "observed_ms": last_t, "alive": alive}
    if not alive:
        board.expect_power_cycle("FEED 200/100")
    assert alive, f"плата сброшена через {last_t} мс кормления 5 Гц — сброс не от LED_BLNK?"


# ---------------------------------------------------------------------------
# 4. Максимальный период кормления
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("fraction", [0.25, 0.5, 0.75, 0.9])
def test_feed_period_sweep(board: WdtBoard, fraction: float):
    t_min = min(RESULTS.all_timeouts())
    period = max(2, int(t_min * fraction))
    _loaded_and_quiet(board)
    assert board.cmd(f"FEED {period} {period // 2}") == "OK"
    alive, _ = board.watch(FEED_OBSERVE_FACTOR * t_min / 1000.0)
    RESULTS.sweep.append({"fraction": fraction, "period_ms": period, "alive": alive})
    if not alive:
        board.expect_power_cycle(f"FEED период {period}")
    if fraction <= 0.25:
        assert alive, f"период {period} мс (¼ таймаута) не удержал плату"


def test_feed_period_sweep_is_monotonic():
    """Если период P держит плату, любой период короче P тоже должен держать."""
    rows = sorted(RESULTS.sweep, key=lambda r: r["period_ms"])
    if not rows:
        pytest.skip("развёртка не выполнялась")
    first_dead = next((i for i, r in enumerate(rows) if not r["alive"]), len(rows))
    assert all(not r["alive"] for r in rows[first_dead:]), f"немонотонная развёртка: {rows}"


# ---------------------------------------------------------------------------
# 5. Хватает ли короткого импульса (сторож видит фронты через C73)
# ---------------------------------------------------------------------------


def test_short_pulse_feeds(board: WdtBoard):
    t_min = min(RESULTS.all_timeouts())
    period = max(2, t_min // 4)
    _loaded_and_quiet(board)
    assert board.cmd(f"FEED {period} 1") == "OK"
    alive, _ = board.watch(FEED_OBSERVE_FACTOR * t_min / 1000.0)
    RESULTS.short_pulse = {"period_ms": period, "on_ms": 1, "alive": alive}
    if not alive:
        board.expect_power_cycle(f"импульс 1 мс / {period}")


# ---------------------------------------------------------------------------
# 6. Минимальная ширина импульса (обе полярности)
#
# 1 мс не кормит (прогон 2026-09-25), 50 % меандр кормит при любом периоде.
# Гипотеза: C73/R45 (τ ≈ 0,1 с) — за короткий импульс C73 не успевает
# перезарядиться, и фронт не открывает VT8A. Меряем порог для:
#   LOW  — пин LOW (LED горит) w мс, остальное HIGH;
#   HIGH — пин HIGH (LED не горит) w мс, остальное LOW.
# Период 1 с — типичный для heartbeat. Сбой проявится не позже ~T после
# начала (плата накормлена штатным режимом при загрузке), поэтому хватает
# наблюдения PULSE_OBSERVE_FACTOR × таймаут.
# ---------------------------------------------------------------------------

PULSE_PERIOD_MS = 1000
PULSE_WIDTHS_MS = [2, 5, 10, 20, 50, 100, 200]
PULSE_OBSERVE_FACTOR = 1.5


def _feed_and_watch(board: WdtBoard, period: int, on_ms: int, factor: float, context: str) -> bool:
    t_max = max(RESULTS.all_timeouts())
    _loaded_and_quiet(board)
    assert board.cmd(f"FEED {period} {on_ms}") == "OK"
    alive, _ = board.watch(factor * t_max / 1000.0)
    if not alive:
        board.expect_power_cycle(context)
    return alive


@pytest.mark.parametrize("width", PULSE_WIDTHS_MS)
@pytest.mark.parametrize("polarity", ["LOW", "HIGH"])
def test_pulse_width_sweep(board: WdtBoard, polarity: str, width: int):
    on_ms = width if polarity == "LOW" else PULSE_PERIOD_MS - width
    alive = _feed_and_watch(board, PULSE_PERIOD_MS, on_ms, PULSE_OBSERVE_FACTOR,
                            f"импульс {polarity} {width} мс")
    RESULTS.pulse_width.append(
        {"polarity": polarity, "width_ms": width, "period_ms": PULSE_PERIOD_MS, "alive": alive}
    )


# Порог LOW между 50 и 100 мс (2026-09-25) — уточняем.
PULSE_WIDTHS_FINE_MS = [60, 70, 80, 90]


@pytest.mark.parametrize("width", PULSE_WIDTHS_FINE_MS)
def test_pulse_width_fine_low(board: WdtBoard, width: int):
    alive = _feed_and_watch(board, PULSE_PERIOD_MS, width, PULSE_OBSERVE_FACTOR,
                            f"импульс LOW {width} мс")
    RESULTS.pulse_width.append(
        {"polarity": "LOW", "width_ms": width, "period_ms": PULSE_PERIOD_MS, "alive": alive}
    )


def test_pulse_width_sweep_is_monotonic():
    """Если ширина w кормит, любая шире w (той же полярности) тоже должна кормить."""
    for polarity in ("LOW", "HIGH"):
        rows = sorted((r for r in RESULTS.pulse_width if r["polarity"] == polarity),
                      key=lambda r: r["width_ms"])
        first_alive = next((i for i, r in enumerate(rows) if r["alive"]), len(rows))
        assert all(r["alive"] for r in rows[first_alive:]), f"немонотонно ({polarity}): {rows}"


# ---------------------------------------------------------------------------
# 7. Реальные паттерны прошивок — кормят ли они сторожа как есть
# ---------------------------------------------------------------------------

REAL_PATTERNS = {
    # firmware/bootloader/src/led_status.c: LED_HEARTBEAT_ON_MS / _PERIOD_MS.
    # Было 50/500 — НЕ кормило (2026-09-25); исправлено на 150/500.
    "bootloader HEARTBEAT 150/500": (500, 150),
    # led_status.c: LED_RECOVERY_ON_MS / _PERIOD_MS (оба LED). Было 100/200 (запас ×1,4) → 200/400.
    "bootloader RECOVERY 200/400": (400, 200),
    # firmware/test/src/main.c: toggle каждые CONNECT_BLINK_MS=200 (до подключения CDC).
    "firmware_test CONNECT 200/400": (400, 200),
}


# Известный дефект: ждёт исправления паттерна bootloader (PLAN.md, фаза 3). strict — как только
# паттерн начнёт кормить, тест потребует обновить константу и снять пометку.
_KNOWN_BAD: set[str] = set()  # был «bootloader HEARTBEAT 50/500» — исправлен


@pytest.mark.parametrize(
    "name",
    [pytest.param(n, marks=pytest.mark.xfail(strict=True, reason="LOW 50 мс < порога сторожа"))
     if n in _KNOWN_BAD else n for n in REAL_PATTERNS],
)
def test_real_pattern_feeds(board: WdtBoard, name: str):
    period, on_ms = REAL_PATTERNS[name]
    alive = _feed_and_watch(board, period, on_ms, FEED_OBSERVE_FACTOR, name)
    RESULTS.patterns.append({"name": name, "period_ms": period, "on_ms": on_ms, "alive": alive})
    assert alive, f"паттерн «{name}» НЕ кормит аппаратный сторож V3 — его надо менять"
