"""
conftest.py — pytest-фикстуры для HIL-тестов MIMXRT1052.
Вся работа с железом делегирована pyocd_utils.
"""

from __future__ import annotations

import logging
import os
import time
from pathlib import Path
from typing import Generator

import pytest
import serial

from pyocd_utils import flexram_init, load_elf, open_target, run_from_vectors

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Конфигурация — переопределяется через переменные окружения
# ---------------------------------------------------------------------------
_REPO_ROOT = Path(__file__).resolve().parents[3]
_BUILD_DIR = Path(os.environ.get("HIL_BUILD_DIR",
                                  _REPO_ROOT / "build" / "target-debug"))
_VCOM_PORT = os.environ.get("HIL_VCOM_PORT", "/dev/tty.usbmodemGUXFBWDJBWTGQ3")
_VCOM_BAUD = int(os.environ.get("HIL_VCOM_BAUD", "115200"))
_READY_TIMEOUT = float(os.environ.get("HIL_READY_TIMEOUT", "5.0"))


# ---------------------------------------------------------------------------
# CLI-опции pytest
# ---------------------------------------------------------------------------
def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--elf",     default=None,       help="Путь к .elf файлу")
    parser.addoption("--vcom",    default=_VCOM_PORT,  help="VCOM-порт MCU-Link")
    parser.addoption("--no-load", action="store_true", help="ELF уже запущен")


# ---------------------------------------------------------------------------
# Внутренняя функция загрузки
# ---------------------------------------------------------------------------
def _load_elf(request: pytest.FixtureRequest, default_elf: Path) -> None:
    if request.config.getoption("--no-load"):
        log.info("--no-load: пропускаем загрузку ELF")
        return

    elf = Path(request.config.getoption("--elf") or default_elf)
    assert elf.exists(), f"ELF не найден: {elf}"

    with open_target() as target:
        flexram_init(target)
        load_elf(target, str(elf))
        run_from_vectors(target)

    log.info("ELF загружен: %s", elf.name)


# ---------------------------------------------------------------------------
# Фикстуры загрузки (scope=module — один раз на файл с тестами)
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module")
def loaded_firmware_test(request: pytest.FixtureRequest) -> None:
    _load_elf(request, _BUILD_DIR / "firmware/test/firmware_test.elf")


@pytest.fixture(scope="module")
def loaded_host_uart(request: pytest.FixtureRequest) -> None:
    _load_elf(
        request,
        _BUILD_DIR / "tests/target/host_uart/test_host_uart.elf",
    )


# ---------------------------------------------------------------------------
# Фикстура UART — открывает порт и ждёт "READY\r\n" от прошивки
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module")
def uart(
    request: pytest.FixtureRequest,
    loaded_host_uart,           # гарантирует порядок: сначала загрузка
) -> Generator[serial.Serial, None, None]:

    port = request.config.getoption("--vcom")
    log.info("UART %s @ %d baud", port, _VCOM_BAUD)

    ser = serial.Serial(
        port=port,
        baudrate=_VCOM_BAUD,
        timeout=2.0,
        write_timeout=1.0,
    )

    # Ждём "READY" — прошивка отправляет его один раз при старте
    deadline = time.monotonic() + _READY_TIMEOUT
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
            f"Прошивка не отправила READY за {_READY_TIMEOUT} с. "
            "Проверьте VCOM-порт и bsp_uart_host_init()."
        )

    ser.reset_input_buffer()
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