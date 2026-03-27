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

import pytest
import serial

import env_config as cfg
from pyocd_utils import flexram_init, load_elf, open_target, run_from_vectors

log = logging.getLogger(__name__)

# Задержка после включения питания таргета (мс стабилизации + POR)
_POWER_ON_SETTLE_S = 1.0


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
def _open_uart_and_wait_ready(request: pytest.FixtureRequest) -> serial.Serial:
    """Открыть VCOM, дождаться READY от прошивки."""
    port = request.config.getoption("--vcom")
    log.info("UART %s @ %d baud", port, cfg.VCOM_BAUD)

    ser = serial.Serial(
        port=port,
        baudrate=cfg.VCOM_BAUD,
        timeout=2.0,
        write_timeout=1.0,
    )

    # Ждём READY
    deadline = time.monotonic() + cfg.READY_TIMEOUT
    ready = False
    while time.monotonic() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if line == "READY":
            ready = True
            log.info("Получен READY от прошивки")
            break

    if not ready:
        ser.close()
        pytest.fail(
            f"Прошивка не отправила READY за {cfg.READY_TIMEOUT} с — "
            "проверьте VCOM-порт и bsp_uart_host_init()."
        )

    ser.reset_input_buffer()
    return ser


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
    log.info("Питание таргета включено, ждём %.1f с", _POWER_ON_SETTLE_S)
    time.sleep(_POWER_ON_SETTLE_S)

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
def loaded_host_uart(request: pytest.FixtureRequest) -> None:
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/host_uart/test_host_uart.elf",
    )


@pytest.fixture(scope="module")
def loaded_hil_opto(request: pytest.FixtureRequest, m5: M5Agent) -> None:
    """
    Загрузить test_hil_opto.elf.

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


# ---------------------------------------------------------------------------
# Фикстуры UART
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module")
def uart(
    request: pytest.FixtureRequest,
    loaded_host_uart,
) -> Generator[serial.Serial, None, None]:
    ser = _open_uart_and_wait_ready(request)
    yield ser
    ser.close()


@pytest.fixture(scope="module")
def uart_opto(
    request: pytest.FixtureRequest,
    loaded_hil_opto,
) -> Generator[serial.Serial, None, None]:
    ser = _open_uart_and_wait_ready(request)
    yield ser
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