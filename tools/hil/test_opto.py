"""
test_opto.py — HIL тест bsp_opto через M5StampPLC.

Стенд:
  M5StampPLC реле → оптопары таргета (PS2801-4, active-LOW):
    RLY2 → EXT_IN1 (ch1, BSP_OPTO_CH_IN1)
    RLY3 → EXT_IN2 (ch2, BSP_OPTO_CH_IN2)
    RLY4 → RS_RX   (ch3, BSP_OPTO_CH_RS)

Схема фикстур:
  conftest.loaded_hil_opto → pyocd_utils: FLEXRAM + load ELF + run
  conftest.uart_opto       → pyserial: открыть VCOM, ждать READY
  conftest.m5              → M5Agent: JSON-lines к M5StampPLC
  тесты                    → uart_cmd() + m5.opto_set() → assert

Запуск:
  just host::hil-opto
  uv run pytest test_opto.py -v
  uv run pytest test_opto.py -v --no-load --m5-port /dev/ttyACM1
"""

import time

import pytest
from conftest import uart_cmd

# Задержка после переключения реле перед чтением состояния (с).
# Включает: механическое переключение реле (~5ms), время отклика
# оптопары (~50µs), ISR + process() цикл, UART round-trip (~2ms).
RELAY_SETTLE_S = 0.05


# ── Связь ──────────────────────────────────────────────────────────────────

class TestOptoConnectivity:
    """Проверка каналов связи с таргетом и M5."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_opto, m5):
        self.ser = uart_opto
        self.m5 = m5

    def test_target_ping(self):
        """PING → PONG: канал host↔target работает."""
        assert uart_cmd(self.ser, "PING") == "PONG"

    def test_m5_ping(self):
        """M5 agent отвечает на ping."""
        self.m5.ping()


# ── Состояние по умолчанию ─────────────────────────────────────────────────

class TestOptoReadDefault:
    """Все каналы INACTIVE без внешнего воздействия."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_opto, m5):
        self.ser = uart_opto
        self.m5 = m5
        self.m5.opto_all_off()
        time.sleep(RELAY_SETTLE_S)

    def test_ch1_default_inactive(self):
        assert uart_cmd(self.ser, "OPTO_READ 1") == "INACTIVE"

    def test_ch2_default_inactive(self):
        assert uart_cmd(self.ser, "OPTO_READ 2") == "INACTIVE"

    def test_ch3_default_inactive(self):
        """RS_RX (ch3) — сконфигурирован как GPIO, должен быть INACTIVE."""
        assert uart_cmd(self.ser, "OPTO_READ 3") == "INACTIVE"


# ── Активация / деактивация каналов ────────────────────────────────────────

class TestOptoActivateDeactivate:
    """Активация/деактивация каждого канала по отдельности через M5."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_opto, m5):
        self.ser = uart_opto
        self.m5 = m5
        self.m5.opto_all_off()
        time.sleep(RELAY_SETTLE_S)

    @pytest.mark.parametrize("ch", [1, 2, 3])
    def test_activate_single_channel(self, ch):
        """M5 opto ON → таргет читает ACTIVE."""
        self.m5.opto_set(ch, True)
        time.sleep(RELAY_SETTLE_S)
        assert uart_cmd(self.ser, f"OPTO_READ {ch}") == "ACTIVE"

    @pytest.mark.parametrize("ch", [1, 2, 3])
    def test_deactivate_single_channel(self, ch):
        """ON → OFF → таргет читает INACTIVE."""
        self.m5.opto_set(ch, True)
        time.sleep(RELAY_SETTLE_S)
        self.m5.opto_set(ch, False)
        time.sleep(RELAY_SETTLE_S)
        assert uart_cmd(self.ser, f"OPTO_READ {ch}") == "INACTIVE"

    @pytest.mark.parametrize("ch", [1, 2, 3])
    def test_isolation(self, ch):
        """Активация одного канала не влияет на остальные."""
        others = [c for c in [1, 2, 3] if c != ch]
        self.m5.opto_set(ch, True)
        time.sleep(RELAY_SETTLE_S)
        for other in others:
            assert uart_cmd(self.ser, f"OPTO_READ {other}") == "INACTIVE", \
                f"ch{other} должен быть INACTIVE когда активен только ch{ch}"


