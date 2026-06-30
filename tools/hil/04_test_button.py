"""test_hil_button.py — интерактивный HIL-тест bsp_button.

Тип: базовый (без M5StampPLC).
Кнопки расположены на плате таргета — оператор нажимает их вручную
по подсказкам. Запускать с флагом -s:

    just host::hil-button
    # или вручную:
    uv run --directory tools/hil pytest test_hil_button.py -v -s
"""

import time

import pytest

from conftest import uart_cmd

# Время после нажатия/отпускания, чтобы debounce (20 мс) гарантированно сработал.
# Берём с запасом: 4 × 5 мс poll + накладные расходы CLI.
DEBOUNCE_SETTLE_S = 0.10


def _operator_prompt(msg: str) -> None:
    """Печатает подсказку оператору и ждёт Enter."""
    input(f"\n  >>> {msg}\n      Нажмите Enter когда готово...")
    time.sleep(DEBOUNCE_SETTLE_S)


@pytest.mark.interactive
class TestHilButton:
    @pytest.fixture(autouse=True)
    def _setup(self, uart_button):
        self.ser = uart_button

    # ------------------------------------------------------------------
    # Базовая связь
    # ------------------------------------------------------------------

    def test_ping(self):
        """Проверка UART-канала."""
        assert uart_cmd(self.ser, "PING") == "PONG"

    # ------------------------------------------------------------------
    # Сырое чтение в покое
    # ------------------------------------------------------------------

    def test_button1_idle_raw(self):
        """Кнопка 1 не нажата → READ 0 == 0."""
        _operator_prompt("Убедись что кнопка 1 (TactBut1) НЕ нажата")
        assert uart_cmd(self.ser, "READ 0") == "0"

    def test_button2_idle_raw(self):
        """Кнопка 2 не нажата → READ 1 == 0."""
        _operator_prompt("Убедись что кнопка 2 (TactBut2) НЕ нажата")
        assert uart_cmd(self.ser, "READ 1") == "0"

    # ------------------------------------------------------------------
    # Сырое чтение при нажатии
    # ------------------------------------------------------------------

    def test_button1_raw_press(self):
        """Нажатая кнопка 1 → READ 0 == 1."""
        _operator_prompt("Нажми и УДЕРЖИ кнопку 1 (TactBut1)")
        assert uart_cmd(self.ser, "READ 0") == "1"

    def test_button1_raw_release(self):
        """После отпускания → READ 0 == 0."""
        _operator_prompt("ОТПУСТИ кнопку 1 (TactBut1)")
        assert uart_cmd(self.ser, "READ 0") == "0"

    def test_button2_raw_press(self):
        """Нажатая кнопка 2 → READ 1 == 1."""
        _operator_prompt("Нажми и УДЕРЖИ кнопку 2 (TactBut2)")
        assert uart_cmd(self.ser, "READ 1") == "1"

    def test_button2_raw_release(self):
        """После отпускания → READ 1 == 0."""
        _operator_prompt("ОТПУСТИ кнопку 2 (TactBut2)")
        assert uart_cmd(self.ser, "READ 1") == "0"

    # ------------------------------------------------------------------
    # Debounce: событие нажатия
    # ------------------------------------------------------------------

    def test_button1_debounce_pressed_event(self):
        """Нажать и отпустить кнопку 1 → EVENT_P 0 == 1."""
        _operator_prompt("Нажми и отпусти кнопку 1 (TactBut1)")
        assert uart_cmd(self.ser, "EVENT_P 0") == "1"

    def test_button1_event_consumed(self):
        """EVENT_P сбрасывается после первого вызова."""
        # Флаг должен быть уже сброшен предыдущим тестом
        assert uart_cmd(self.ser, "EVENT_P 0") == "0"

    def test_button2_debounce_pressed_event(self):
        """Нажать и отпустить кнопку 2 → EVENT_P 1 == 1."""
        _operator_prompt("Нажми и отпусти кнопку 2 (TactBut2)")
        assert uart_cmd(self.ser, "EVENT_P 1") == "1"

    # ------------------------------------------------------------------
    # Debounce: событие отпускания
    # ------------------------------------------------------------------

    def test_button1_released_event(self):
        """Нажать, удержать, отпустить → EVENT_R 0 == 1."""
        _operator_prompt("Нажми кнопку 1 (TactBut1), подожди секунду, затем отпусти")
        assert uart_cmd(self.ser, "EVENT_R 0") == "1"

    def test_button1_released_event_consumed(self):
        """EVENT_R сбрасывается после первого вызова."""
        assert uart_cmd(self.ser, "EVENT_R 0") == "0"

    # ------------------------------------------------------------------
    # Стабильное состояние
    # ------------------------------------------------------------------

    def test_button1_stable_state_while_held(self):
        """STATE 0 == 1 пока кнопка удерживается."""
        _operator_prompt("Нажми и УДЕРЖИ кнопку 1 (TactBut1)")
        assert uart_cmd(self.ser, "STATE 0") == "1"

    def test_button1_stable_state_after_release(self):
        """STATE 0 == 0 после отпускания."""
        _operator_prompt("ОТПУСТИ кнопку 1 (TactBut1)")
        assert uart_cmd(self.ser, "STATE 0") == "0"

    # ------------------------------------------------------------------
    # Независимость кнопок
    # ------------------------------------------------------------------

    def test_buttons_independent_btn2_only(self):
        """Нажата только кнопка 2 → STATE 0 == 0, STATE 1 == 1."""
        _operator_prompt("Нажми и УДЕРЖИ ТОЛЬКО кнопку 2 (TactBut2), кнопка 1 свободна")
        assert uart_cmd(self.ser, "STATE 0") == "0", "Кнопка 1 не должна быть нажата"
        assert uart_cmd(self.ser, "STATE 1") == "1", "Кнопка 2 должна быть нажата"
        _operator_prompt("ОТПУСТИ кнопку 2 (TactBut2)")
