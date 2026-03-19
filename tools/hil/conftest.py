"""
conftest.py — pytest-фикстуры для HIL-тестов MIMXRT1052.
Вся работа с железом делегирована pyocd_utils.
Конфигурация читается из env_config (приоритет: env > .env > default).
"""

from __future__ import annotations

import logging
import time
from pathlib import Path
from typing import Generator

import pytest
import serial

import env_config as cfg
from pyocd_utils import flexram_init, load_elf, open_target, run_from_vectors

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# CLI-опции pytest (перекрывают .env и os.environ)
# ---------------------------------------------------------------------------
def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--elf",     default=None,          help="Путь к .elf файлу")
    parser.addoption("--vcom",    default=cfg.VCOM_PORT,  help="VCOM-порт MCU-Link")
    parser.addoption("--no-load", action="store_true",    help="ELF уже запущен")


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
# Фикстуры загрузки (scope=module — один раз на файл с тестами)
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def loaded_host_uart(request: pytest.FixtureRequest) -> None:
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/host_uart/test_host_uart.elf",
    )


# ---------------------------------------------------------------------------
# Фикстура UART — открывает порт и ждёт "READY\r\n" от прошивки
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module")
def uart(
    request: pytest.FixtureRequest,
    loaded_host_uart,
) -> Generator[serial.Serial, None, None]:

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