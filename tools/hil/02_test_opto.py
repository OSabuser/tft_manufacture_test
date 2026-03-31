"""
test_opto.py — HIL тест bsp_opto через M5StampPLC.

Стенд:
  M5StampPLC реле → оптопары таргета (PS2801-4, active-HIGH, неинвертирующие):
    RLY2 → EXT_IN1 (ch1, BSP_OPTO_CH_IN1)
    RLY3 → EXT_IN2 (ch2, BSP_OPTO_CH_IN2)
    RLY4 → RS_RX   (ch3, BSP_OPTO_CH_RS)

Цепочка фикстур (scope=module, создаются один раз на весь файл):

  m5 (питание ON + opto_all_off)
   └── loaded_hil_opto (грузит ELF через pyOCD)
         └── uart_opto (открывает VCOM, ждёт READY)
               └── _setup (autouse, function scope) → self.ser / self.m5

Запуск:
  just host::hil-opto
  uv run pytest test_opto.py -v
  uv run pytest test_opto.py -v --no-load --m5-port /dev/ttyACM1
"""

import time

import pytest
from conftest import uart_cmd

# ---------------------------------------------------------------------------
# Временны́е константы
# ---------------------------------------------------------------------------

# После переключения реле ждём:
#   реле механика  ~10 мс
#   оптопара       ~0.1 мс
#   debounce       10 мс  (debounce_ms в прошивке)
#   process() цикл ~10 мс (CLI_RX_TIMEOUT)
#   UART round-trip ~2 мс
# Итого минимум ~33 мс. Берём с запасом × 3.
RELAY_ON_S  = 0.1   # ждать после включения реле

# После выключения нужно убедиться что INACTIVE подтверждён дебаунсом.
# Берём чуть больше чем RELAY_ON_S — дебаунс работает симметрично.
RELAY_OFF_S = 0.1


# ---------------------------------------------------------------------------
# Вспомогательные функции
# ---------------------------------------------------------------------------

def opto_read(ser, ch: int) -> str:
    """Прочитать состояние канала (1-based). Возвращает 'ACTIVE' или 'INACTIVE'."""
    return uart_cmd(ser, f"OPTO_READ {ch}")


def reset_events(ser) -> None:
    """Сбросить счётчик событий на таргете."""
    assert uart_cmd(ser, "OPTO_RESET_EVENTS") == "OK"


def all_off_and_confirm(m5, ser, timeout_s: float = 1.0) -> None:
    """
    Выключить все реле и дождаться пока ВСЕ каналы подтвердят INACTIVE.

    Простого time.sleep() недостаточно: дебаунс + цикл process() могут
    занимать переменное время. Активно опрашиваем таргет пока не убедимся
    что состояние стабильно.
    """
    m5.opto_all_off()

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        time.sleep(0.02)
        states = [opto_read(ser, ch) for ch in [1, 2, 3]]
        if all(s == "INACTIVE" for s in states):
            return

    # Если вышли по таймауту — сообщаем что именно осталось активным
    states = [opto_read(ser, ch) for ch in [1, 2, 3]]
    active = [i + 1 for i, s in enumerate(states) if s == "ACTIVE"]
    raise TimeoutError(
        f"Каналы {active} остались ACTIVE после opto_all_off() "
        f"и ожидания {timeout_s} с. Проверьте схему стенда."
    )


# ---------------------------------------------------------------------------
# Проверка каналов связи
# ---------------------------------------------------------------------------

class TestOptoConnectivity:
    """Базовая проверка: таргет и M5 отвечают."""

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


# ---------------------------------------------------------------------------
# Состояние по умолчанию (все реле выключены)
# ---------------------------------------------------------------------------

class TestOptoReadDefault:
    """Все каналы INACTIVE без внешнего воздействия."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_opto, m5):
        self.ser = uart_opto
        self.m5 = m5
        all_off_and_confirm(self.m5, self.ser)

    def test_ch1_default_inactive(self):
        assert opto_read(self.ser, 1) == "INACTIVE"

    def test_ch2_default_inactive(self):
        assert opto_read(self.ser, 2) == "INACTIVE"

    def test_ch3_default_inactive(self):
        """RS_RX (ch3) — сконфигурирован как GPIO (MODE_LEVEL), должен быть INACTIVE."""
        assert opto_read(self.ser, 3) == "INACTIVE"


# ---------------------------------------------------------------------------
# Активация / деактивация каналов
# ---------------------------------------------------------------------------

class TestOptoActivateDeactivate:
    """Активация/деактивация каждого канала по отдельности через M5."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_opto, m5):
        self.ser = uart_opto
        self.m5 = m5
        all_off_and_confirm(self.m5, self.ser)

    @pytest.mark.parametrize("ch", [1, 2, 3])
    def test_activate_single_channel(self, ch):
        """M5 opto ON → таргет читает ACTIVE."""
        self.m5.opto_set(ch, True)
        time.sleep(RELAY_ON_S)
        assert opto_read(self.ser, ch) == "ACTIVE"

    @pytest.mark.parametrize("ch", [1, 2, 3])
    def test_deactivate_single_channel(self, ch):
        """ON → OFF → таргет читает INACTIVE."""
        self.m5.opto_set(ch, True)
        time.sleep(RELAY_ON_S)
        self.m5.opto_set(ch, False)
        time.sleep(RELAY_OFF_S)
        assert opto_read(self.ser, ch) == "INACTIVE"

    @pytest.mark.parametrize("ch", [1, 2, 3])
    def test_isolation(self, ch):
        """Активация одного канала не влияет на остальные."""
        others = [c for c in [1, 2, 3] if c != ch]
        self.m5.opto_set(ch, True)
        time.sleep(RELAY_ON_S)
        for other in others:
            assert opto_read(self.ser, other) == "INACTIVE", \
                f"ch{other} должен быть INACTIVE когда активен только ch{ch}"


# ---------------------------------------------------------------------------
# Callback-события (проверяет ISR + debounce + bsp_opto_process)
# ---------------------------------------------------------------------------

class TestOptoEvents:
    """
    Проверяет что прерывание + bsp_opto_process() + коллбэк корректно
    регистрируют события.

    Таргет считает события через s_event_count / s_last_ch / s_last_state,
    доступные через команды OPTO_EVENTS / OPTO_LAST_EVENT / OPTO_RESET_EVENTS.
    """

    @pytest.fixture(autouse=True)
    def _setup(self, uart_opto, m5):
        self.ser = uart_opto
        self.m5 = m5
        all_off_and_confirm(self.m5, self.ser)
        reset_events(self.ser)

    def test_activate_generates_event(self):
        """Включение реле генерирует минимум одно событие ACTIVE."""
        self.m5.opto_set(1, True)
        time.sleep(RELAY_ON_S)

        count = int(uart_cmd(self.ser, "OPTO_EVENTS"))
        assert count >= 1, f"Ожидали >= 1 событие, получили {count}"

        last = uart_cmd(self.ser, "OPTO_LAST_EVENT")
        ch_str, state_str = last.split()
        assert ch_str == "1"
        assert state_str == "ACTIVE"

    def test_deactivate_generates_event(self):
        """Выключение реле генерирует событие INACTIVE."""
        self.m5.opto_set(1, True)
        time.sleep(RELAY_ON_S)
        reset_events(self.ser)

        self.m5.opto_set(1, False)
        time.sleep(RELAY_OFF_S)

        count = int(uart_cmd(self.ser, "OPTO_EVENTS"))
        assert count >= 1, f"Ожидали >= 1 событие после выключения, получили {count}"

        last = uart_cmd(self.ser, "OPTO_LAST_EVENT")
        ch_str, state_str = last.split()
        assert ch_str == "1"
        assert state_str == "INACTIVE"

    def test_reset_events_clears_counter(self):
        """OPTO_RESET_EVENTS обнуляет счётчик."""
        self.m5.opto_set(1, True)
        time.sleep(RELAY_ON_S)

        reset_events(self.ser)
        assert uart_cmd(self.ser, "OPTO_EVENTS") == "0"

    def test_correct_channel_reported_in_last_event(self):
        """OPTO_LAST_EVENT сообщает правильный канал."""
        self.m5.opto_set(2, True)
        time.sleep(RELAY_ON_S)

        last = uart_cmd(self.ser, "OPTO_LAST_EVENT")
        ch_str, _ = last.split()
        assert ch_str == "2", f"Ожидали ch=2, получили {ch_str!r}"