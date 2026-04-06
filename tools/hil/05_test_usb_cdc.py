"""
05_test_usb_cdc.py — HIL тест bsp_usb_cdc.

Схема:
  UART (MCU-Link VCOM)  — управляющий канал (PING/PONG, USB_READY)
  USB CDC (TFT Board)   — тестируемый канал (echo)

  conftest.loaded_hil_usb_cdc → pyocd_utils: load ELF
  conftest.uart_hil_usb_cdc   → UART: ждать READY
  conftest.usb_cdc_port       → USB CDC: открыть порт, установить DTR

Запуск:
  just host::hil-usb-cdc
  uv run --directory tools/hil pytest 05_test_usb_cdc.py -v

Переменные окружения:
  HIL_USB_CDC_PORT  — порт USB CDC устройства (/dev/cu.usbmodemXXXX)
  HIL_USB_CDC_BAUD  — baudrate (по умолчанию 115200)
"""

import time

import pytest
from conftest import uart_cmd

# Таймаут чтения USB CDC — достаточный для HS bulk transfer.
USB_CDC_READ_TIMEOUT_S = 1.0


def usb_cdc_echo(ser, data: bytes, timeout_s: float = USB_CDC_READ_TIMEOUT_S) -> bytes:
    """Отправить данные по USB CDC и прочитать echo."""
    ser.reset_input_buffer()
    ser.write(data)
    ser.flush()

    result = b""
    deadline = time.monotonic() + timeout_s

    while len(result) < len(data) and time.monotonic() < deadline:
        chunk = ser.read(len(data) - len(result))
        if chunk:
            result += chunk

    return result

@pytest.mark.usb_vcom
class TestUsbCdcBasic:
    """Базовые тесты USB CDC ACM — enumeration, echo, payload sizes."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_hil_usb_cdc, usb_cdc_port):
        self.uart = uart_hil_usb_cdc
        self.cdc = usb_cdc_port

    # ---- UART control channel ----

    def test_uart_ping(self):
        """UART канал работает — прошивка запущена."""
        assert uart_cmd(self.uart, "PING") == "PONG"

    def test_usb_ready(self):
        """USB CDC enumeration завершён, хост подключён."""
        assert uart_cmd(self.uart, "USB_READY") == "1"

    # ---- USB CDC echo ----

    def test_echo_short(self):
        """Echo 5 байт — минимальный пакет."""
        data = b"hello"
        assert usb_cdc_echo(self.cdc, data) == data

    def test_echo_with_newlines(self):
        """Echo с CR/LF — проверка что USB CDC бинарный, не line-based."""
        data = b"line1\r\nline2\r\n"
        assert usb_cdc_echo(self.cdc, data) == data

    def test_echo_binary(self):
        """Echo бинарных данных — все значения 0x00..0xFF."""
        data = bytes(range(256))
        assert usb_cdc_echo(self.cdc, data) == data

    def test_echo_repeated(self):
        """10 echo подряд — нет зависаний, нет потерь."""
        for i in range(10):
            data = f"packet_{i:03d}".encode()
            result = usb_cdc_echo(self.cdc, data)
            assert result == data, f"Сбой на итерации {i}: {result!r} != {data!r}"

    def test_echo_64_bytes(self):
        """Echo 64 байт — граница FS bulk packet."""
        data = b"A" * 64
        assert usb_cdc_echo(self.cdc, data) == data

    def test_echo_512_bytes(self):
        """Echo 512 байт — граница HS bulk packet (BSP_USB_CDC_MAX_PACKET_SIZE)."""
        data = bytes([i & 0xFF for i in range(512)])
        assert usb_cdc_echo(self.cdc, data) == data

    # ---- Error recovery ----

    def test_uart_after_cdc(self):
        """UART канал работает после серии USB CDC операций."""
        assert uart_cmd(self.uart, "PING") == "PONG"