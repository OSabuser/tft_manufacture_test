"""
test_uart.py — HIL тест bsp_uart_host.

Схема:
  conftest.loaded_host_uart → pyocd_utils: FLEXRAM + load ELF + run
  conftest.uart             → pyserial: открыть VCOM, ждать READY
  тесты                     → uart_cmd() → assert

Запуск:
  uv run pytest test_uart.py -v
  uv run pytest test_uart.py -v -m smoke
  uv run pytest test_uart.py -v --no-load   # ELF уже запущен
"""

import pytest
from conftest import uart_cmd


class TestUartBasic:
    """Базовые тесты UART CLI — канал host↔target."""

    @pytest.fixture(autouse=True)
    def _setup(self, loaded_host_uart, uart):
        """loaded_host_uart гарантирует что ELF загружен и READY получен."""
        self.ser = uart

    # ------------------------------------------------------------------
    @pytest.mark.smoke
    def test_ping(self):
        """PING → PONG: канал работает в обе стороны."""
        assert uart_cmd(self.ser, "PING") == "PONG"

    @pytest.mark.smoke
    def test_ping_repeated(self):
        """Десять PING подряд — нет зависаний, нет потерь."""
        for i in range(10):
            assert uart_cmd(self.ser, "PING") == "PONG", f"Сбой на итерации {i}"

    # ------------------------------------------------------------------
    def test_echo_simple(self):
        assert uart_cmd(self.ser, "ECHO hello") == "hello"

    def test_echo_with_spaces(self):
        assert uart_cmd(self.ser, "ECHO hello world") == "hello world"

    def test_echo_digits(self):
        assert uart_cmd(self.ser, "ECHO 12345") == "12345"

    def test_echo_max_payload(self):
        """Строка близкая к CLI_LINE_MAX (128) - 5 (prefix 'ECHO ')."""
        payload = "x" * 100
        assert uart_cmd(self.ser, f"ECHO {payload}") == payload

    # ------------------------------------------------------------------
    def test_buf_size(self):
        """UART_BUF_SIZE возвращает значение совпадающее с прошивкой (512)."""
        resp = uart_cmd(self.ser, "UART_BUF_SIZE")
        assert resp.isdigit(), f"Ожидалось число, получили: {resp!r}"
        assert int(resp) == 512

    # ------------------------------------------------------------------
    def test_unknown_command(self):
        """Неизвестная команда → ERR_UNKNOWN (прошивка не зависает)."""
        assert uart_cmd(self.ser, "FOOBAR") == "ERR_UNKNOWN"

    def test_unknown_then_ping(self):
        """После неизвестной команды канал продолжает работать."""
        uart_cmd(self.ser, "FOOBAR")
        assert uart_cmd(self.ser, "PING") == "PONG"