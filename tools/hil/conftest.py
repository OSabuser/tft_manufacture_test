"""
conftest.py — pytest-фикстуры для HIL-тестов MIMXRT1052.
Вся работа с железом делегирована pyocd_utils.
Конфигурация читается из env_config (приоритет: env > .env > default).
"""

from __future__ import annotations

import json
import logging
import time
from pathlib import Path
from typing import Generator
from contextlib import contextmanager
import pytest
import serial

import env_config as cfg
from pyocd_utils import flexram_init, load_elf, open_target, run_from_vectors

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# CLI-опции pytest (перекрывают .env и os.environ)
# ---------------------------------------------------------------------------
def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--elf",      default=None,           help="Путь к .elf файлу")
    parser.addoption("--vcom",     default=cfg.VCOM_PORT,  help="VCOM-порт MCU-Link")
    parser.addoption("--m5-port",  default=None,           help="M5StampPLC serial port")
    parser.addoption("--no-load",  action="store_true",    help="ELF уже запущен")


# ---------------------------------------------------------------------------
# Внутренняя функция загрузки
# ---------------------------------------------------------------------------
def _load_elf(request: pytest.FixtureRequest, default_elf: Path) -> None:
    if request.config.getoption("--no-load"):
        log.info("--no-load: пропускаем загрузку ELF")
        return

    elf = Path(request.config.getoption("--elf") or default_elf)
    assert elf.exists(), f"ELF не найден: {elf}"

    with open_target(frequency=cfg.PYOCD_FREQUENCY) as target:
        flexram_init(target)
        load_elf(target, str(elf))
        run_from_vectors(target)

    log.info("ELF загружен: %s", elf.name)


# ---------------------------------------------------------------------------
# Общая логика UART: открыть порт, дождаться READY
# ---------------------------------------------------------------------------
@contextmanager
def _uart_context(port: str, baud: int, ready_timeout: float):
    ser = serial.Serial(port=port, baudrate=baud, timeout=2.0, write_timeout=1.0)
    try:
        deadline = time.monotonic() + ready_timeout
        ready = False
        while time.monotonic() < deadline:
            line = ser.readline().decode("ascii", errors="replace").strip()
            if line == "READY":
                ready = True
                log.info("Получен READY от прошивки")
                break

        if not ready:
            pytest.fail(f"Прошивка не отправила READY за {ready_timeout} с")

        ser.reset_input_buffer()
        yield ser
    finally:
        ser.close()  # гарантированно, всегда


# ---------------------------------------------------------------------------
# M5StampPLC — драйвер для pytest (JSON-lines протокол)
#
# Определяем РАНЬШЕ фикстур, которые его используют в аннотациях.
# ---------------------------------------------------------------------------
class M5Agent:
    """Драйвер M5StampPLC для pytest (JSON-lines протокол через USB CDC)."""

    def __init__(self, ser: serial.Serial) -> None:
        self._ser = ser

    def cmd(self, command: str, **kwargs) -> dict:
        """Отправить JSON-команду, вернуть parsed-ответ. RuntimeError при ok=false."""
        payload = {"cmd": command, **kwargs}
        self._ser.reset_input_buffer()
        self._ser.write((json.dumps(payload) + "\r\n").encode("ascii"))

        deadline = time.monotonic() + cfg.M5_TIMEOUT
        while time.monotonic() < deadline:
            raw = self._ser.readline()
            if not raw:
                continue
            text = raw.decode("ascii", errors="replace").strip()
            if not text or text == "READY" or not text.startswith("{"):
                continue
            try:
                resp = json.loads(text)
            except json.JSONDecodeError:
                continue
            if not resp.get("ok"):
                raise RuntimeError(
                    f"M5 error: {resp.get('err', '?')} (cmd={command})"
                )
            return resp

        raise TimeoutError(f"M5: нет ответа на '{command}'")

    def ping(self) -> None:
        self.cmd("ping")

    def power(self, state: bool) -> None:
        """Включить/выключить питание таргета (RLY1)."""
        self.cmd("power", state=state)

    def opto_set(self, ch: int, state: bool) -> None:
        self.cmd("opto_set", ch=ch, state=state)

    def opto_all_off(self) -> None:
        self.cmd("opto_all_off")
    def info(self) -> dict:
        """Запросить информацию об агенте (включая can_ok)."""
        return self.cmd("info")
 
    def can_send(self, can_id: int, data: list, ext: bool = False) -> None:
        """Отправить CAN-фрейм с шины M5."""
        self.cmd("can_send", id=can_id, data=list(data), ext=ext)
 
    def can_recv(self, timeout_ms: int = 500) -> dict:
        """
        Принять CAN-фрейм на M5.
        Возвращает dict {id, ext, data}.
        Выбрасывает TimeoutError если фрейм не пришёл.
        """
        resp = self.cmd("can_recv", timeout_ms=timeout_ms)
        # cmd() уже выбрасывает RuntimeError при ok=false (включая timeout от агента)
        return resp


# ---------------------------------------------------------------------------
# Фикстура M5StampPLC
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module")
def m5(request: pytest.FixtureRequest) -> Generator[M5Agent, None, None]:
    port = request.config.getoption("--m5-port") or cfg.M5_PORT
    log.info("M5 %s @ %d baud", port, cfg.M5_BAUD)

    ser = serial.Serial(
        port=port,
        baudrate=cfg.M5_BAUD,
        timeout=cfg.M5_TIMEOUT,
        write_timeout=1.0,
    )

    # Ждём READY; fallback на ping если агент уже работает
    deadline = time.monotonic() + cfg.READY_TIMEOUT
    ready = False
    while time.monotonic() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if line == "READY":
            ready = True
            log.info("M5 READY получен")
            break

    agent = M5Agent(ser)
    if not ready:
        try:
            agent.ping()
            log.info("M5 уже работает (READY пропущен, ping OK)")
        except Exception:
            ser.close()
            pytest.fail(
                "M5 agent не отвечает — проверьте HIL_M5_PORT и "
                "что m5/agent.py развёрнут (just host::m5-deploy)."
            )

    # Включаем питание таргета и ждём стабилизации
    agent.power(True)
    log.info("Питание таргета включено, ждём %.1f с", cfg.POWER_ON_SETTLE_S)
    time.sleep(cfg.POWER_ON_SETTLE_S)

    agent.opto_all_off()  # безопасное начальное состояние
    yield agent

    # Teardown: выключить всё
    try:
        agent.opto_all_off()
    except Exception:
        pass
    try:
        agent.power(False)
        log.info("Питание таргета выключено")
    except Exception:
        pass
    ser.close()


# ---------------------------------------------------------------------------
# Фикстуры загрузки (scope=module — один раз на файл с тестами)
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def loaded_host_uart(request: pytest.FixtureRequest, m5: M5Agent) -> None:
    """
    Загрузить test_hil_button.elf на таргет по SWD
 
    Явная зависимость от m5 гарантирует порядок:
      1. m5 создаётся первым → питание таргета включено
      2. только потом pyOCD подключается и грузит ELF
    """
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/host_uart/test_host_uart.elf",
    )

@pytest.fixture(scope="module")
def loaded_hil_button(request: pytest.FixtureRequest, m5: M5Agent) -> None:
    """
    Загрузить test_hil_button.elf на таргет по SWD
 
    Явная зависимость от m5 гарантирует порядок:
      1. m5 создаётся первым → питание таргета включено
      2. только потом pyOCD подключается и грузит ELF
    """
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/hil_button/test_hil_button.elf",
    )

@pytest.fixture(scope="module")
def loaded_hil_opto(request: pytest.FixtureRequest, m5: M5Agent) -> None:
    """
    Загрузить test_hil_opto.elf на таргет по SWD

    Явная зависимость от фикстуры m5 гарантирует порядок:
      1. m5 создаётся первым → питание таргета включено
      2. только потом pyOCD подключается и грузит ELF
    Без этой зависимости pytest мог бы попытаться подключиться
    к MCU пока он ещё обесточен.
    """
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/hil_opto/test_hil_opto.elf",
    )

@pytest.fixture(scope="module")
def loaded_hil_can(request: pytest.FixtureRequest, m5: M5Agent) -> None:
    """
    Загрузить test_hil_can.elf  на таргет по SWD
 
    Явная зависимость от m5 гарантирует порядок:
      1. m5 создаётся первым → питание таргета включено
      2. только потом pyOCD подключается и грузит ELF
    """
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/hil_can/test_hil_can.elf",
    )

@pytest.fixture(scope="module")
def loaded_hil_usb_cdc(request: pytest.FixtureRequest, m5: M5Agent) -> None:
    """
    Загрузить test_hil_usb_cdc.elf  на таргет по SWD
 
    Явная зависимость от m5 гарантирует порядок:
      1. m5 создаётся первым → питание таргета включено
      2. только потом pyOCD подключается и грузит ELF
    """
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/hil_usb_cdc/test_hil_usb_cdc.elf",
    )

# ---------------------------------------------------------------------------
# Фикстуры UART (установление соединения с VCOM программатора NXP MCU-Link)
# Выполняется один раз на тест
# ---------------------------------------------------------------------------
_UART_FIXTURE_MAP = {
    "uart":             "loaded_host_uart",
    "uart_opto":        "loaded_hil_opto",
    "uart_can":         "loaded_hil_can",
    "uart_button":      "loaded_hil_button",
    "uart_hil_usb_cdc": "loaded_hil_usb_cdc",
}
def _make_uart_fixture(loaded_name: str):
    @pytest.fixture(scope="module")
    def _fixture(request: pytest.FixtureRequest) -> Generator[serial.Serial, None, None]:
        request.getfixturevalue(loaded_name)  # триггерит зависимость явно
        port = request.config.getoption("--vcom")
        with _uart_context(port, cfg.VCOM_BAUD, cfg.READY_TIMEOUT) as ser:
            yield ser
    return _fixture

# Регистрируем все фикстуры в пространстве имён модуля одной строкой
for _name, _dep in _UART_FIXTURE_MAP.items():
    globals()[_name] = _make_uart_fixture(_dep)
# ---------------------------------------------------------------------------
# Фикстуры USB CDC (открытие порта, установка DTR)
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module")
def usb_cdc_port(
    uart_hil_usb_cdc,
) -> Generator[serial.Serial, None, None]:
    """Открыть USB CDC порт таргета. Ждёт появления порта и DTR ready."""
    import time

    if not cfg.TARGET_VCOM_PORT:
        pytest.skip("HIL_USB_CDC_PORT not set")

    # Ждём появления USB CDC порта (enumeration после загрузки ELF).
    deadline = time.monotonic() + cfg.TARGET_VCOM_TIMEOUT
    ser = None
    while time.monotonic() < deadline:
        try:
            ser = serial.Serial(port=cfg.TARGET_VCOM_PORT, baudrate=cfg.TARGET_VCOM_BAUD, timeout=0.5)
            break
        except serial.SerialException:
            time.sleep(0.3)

    if ser is None:
        pytest.fail(f"USB CDC port {cfg.TARGET_VCOM_PORT} not available after {cfg.TARGET_VCOM_TIMEOUT}s")

    try:
        # Установить DTR чтобы firmware увидела DTE presence.
        ser.dtr = True
        time.sleep(0.3)
        # Сбросить входной буфер — могут быть мусорные байты от enumeration.
        ser.reset_input_buffer()
        yield ser
    finally:
        ser.close()  


# ---------------------------------------------------------------------------
# Утилита для тестов
# ---------------------------------------------------------------------------
def uart_cmd(ser: serial.Serial, cmd: str) -> str:
    """Отправить команду, получить одну строку ответа."""
    ser.reset_input_buffer()
    ser.write((cmd.strip() + "\r\n").encode("ascii"))
    resp = ser.readline()
    if not resp:
        raise TimeoutError(f"Нет ответа на команду '{cmd}'")
    return resp.decode("ascii", errors="replace").strip()