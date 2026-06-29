"""
confirm_panel.py — виджет панели подтверждения оператора.

Показывается при confirm_request от firmware_test требующем ответа оператора.
Скрыт по умолчанию (CSS-класс 'hidden').

Messages:
    ConfirmPanel.Confirmed(confirmed: bool)  — оператор нажал OK или Нет
"""

from __future__ import annotations

from textual import on
from textual.app import ComposeResult
from textual.css.query import NoMatches
from textual.message import Message
from textual.timer import Timer
from textual.widget import Widget
from textual.widgets import Button, Static

_TICK_S = 1.0


class ConfirmPanel(Widget):
    """
    Панель подтверждения оператора.

    Использование::

        panel = ConfirmPanel()
        panel.show(prompt="Экран залит красным?", timeout_ms=30000)
        # Слушать ConfirmPanel.Confirmed в родительском экране
    """

    class Confirmed(Message):
        """Оператор ответил на confirm_request."""

        def __init__(self, confirmed: bool) -> None:
            super().__init__()
            self.confirmed = confirmed

    DEFAULT_CSS = ""  # стили в diag.tcss

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        self._timer: Timer | None = None
        self._remaining: int = 0
        self._operator_mode: bool = True  # False → режим buttons (нет кнопок)

    def compose(self) -> ComposeResult:
        yield Static("", id="confirm-prompt")
        yield Static("", id="confirm-countdown")
        yield Button("✓ Да", id="confirm-btn-ok", variant="success")
        yield Button("✗ Нет", id="confirm-btn-fail", variant="error")

    # ── Public API ────────────────────────────────────────────────────────────

    def show_operator(self, prompt: str, timeout_ms: int) -> None:
        """Показать панель с кнопками OK/Нет и countdown."""
        self._operator_mode = True
        self._remaining = timeout_ms // 1000
        self._set_prompt(prompt)
        self._set_countdown(self._remaining)
        self._show_buttons(True)
        self.remove_class("hidden")
        self._start_timer()

    def show_buttons_hint(self, prompt: str) -> None:
        """
        Показать инструкцию для теста кнопок.
        Без кнопок OK/Нет — оператор только читает, нажимает физическую кнопку.
        """
        self._operator_mode = False
        self._stop_timer()
        self._set_prompt(f"⌨  {prompt}")
        self._set_countdown("")
        self._show_buttons(False)
        self.remove_class("hidden")

    def hide(self) -> None:
        """Скрыть панель, остановить таймер."""
        self._stop_timer()
        self.add_class("hidden")
        self._show_buttons(True)  # восстановить на следующий раз

    # ── Обработчики ───────────────────────────────────────────────────────────

    @on(Button.Pressed, "#confirm-btn-ok")
    def _on_ok(self) -> None:
        self.hide()
        self.post_message(self.Confirmed(confirmed=True))

    @on(Button.Pressed, "#confirm-btn-fail")
    def _on_fail(self) -> None:
        self.hide()
        self.post_message(self.Confirmed(confirmed=False))

    # ── Таймер countdown ─────────────────────────────────────────────────────

    def _start_timer(self) -> None:
        self._stop_timer()
        self._timer = self.set_interval(_TICK_S, self._tick)

    def _stop_timer(self) -> None:
        if self._timer is not None:
            self._timer.stop()
            self._timer = None

    def _tick(self) -> None:
        self._remaining -= 1
        self._set_countdown(self._remaining)
        if self._remaining <= 0:
            self.hide()
            self.post_message(self.Confirmed(confirmed=False))

    # ── Утилиты ───────────────────────────────────────────────────────────────

    def _set_prompt(self, text: str) -> None:
        try:
            self.query_one("#confirm-prompt", Static).update(f"⚠  {text}")
        except NoMatches:
            pass

    def _set_countdown(self, value: int | str) -> None:
        text = f"{value}с" if isinstance(value, int) and value > 0 else ""
        try:
            self.query_one("#confirm-countdown", Static).update(text)
        except NoMatches:
            pass

    def _show_buttons(self, visible: bool) -> None:
        for btn_id in ("#confirm-btn-ok", "#confirm-btn-fail"):
            try:
                btn = self.query_one(btn_id, Button)
                if visible:
                    btn.remove_class("hidden")
                else:
                    btn.add_class("hidden")
            except NoMatches:
                pass
