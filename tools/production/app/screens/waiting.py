"""
waiting.py — экран ожидания подключения платы.

Опрашивает USB каждые _DETECT_INTERVAL_S секунд через Flasher.
При обнаружении SDP или CDC отправляет DeviceDetected message в App.

Принимает опциональный disconnect_reason — короткое сообщение о причине
возврата на этот экран (например, "Соединение с платой потеряно"),
показывается несколько секунд поверх обычной подсказки, затем исчезает
само. Нужно, чтобы потеря USB во время прошивки/диагностики (см. отчёт,
замечание №4) не выглядела как необъяснимый скачок экрана — сервисник
должен понимать, что это осознанное поведение, а не баг.
"""

from __future__ import annotations

from typing import Optional

from textual.app import ComposeResult
from textual.css.query import NoMatches
from textual.message import Message
from textual.screen import Screen
from textual.timer import Timer
from textual.widgets import Static

from ..flasher import Flasher
from ..models import AppMode
from ..widgets import AppFrame

_DETECT_INTERVAL_S = 1.5
_SPIN_INTERVAL_S = 0.1
_REASON_DISPLAY_S = 4.0
_SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]


class WaitingScreen(Screen):
    """
    Экран ожидания.

    Messages:
        DeviceDetected(mode)  — плата обнаружена, mode: FLASHING | DIAGNOSING
    """

    class DeviceDetected(Message):
        """Плата обнаружена."""

        def __init__(self, mode: AppMode) -> None:
            super().__init__()
            self.mode = mode

    def __init__(self, disconnect_reason: Optional[str] = None, **kwargs) -> None:
        super().__init__(**kwargs)
        self._disconnect_reason = disconnect_reason
        self._spinner_idx: int = 0
        self._detect_timer: Timer | None = None
        self._spin_timer: Timer | None = None
        self._reason_timer: Timer | None = None

    def compose(self) -> ComposeResult:
        with AppFrame(id="waiting-frame"):
            yield Static("TFT Indicator Board\nService Tool", id="waiting-logo")
            yield Static("", id="waiting-reason", classes="hidden")
            yield Static("Подключите плату индикатора к USB...", id="waiting-hint")
            yield Static(_SPINNER_FRAMES[0], id="waiting-spinner")

    def on_mount(self) -> None:
        self._detect_timer = self.set_interval(_DETECT_INTERVAL_S, self._poll_usb)
        self._spin_timer = self.set_interval(_SPIN_INTERVAL_S, self._spin)
        if self._disconnect_reason:
            self._show_reason(self._disconnect_reason)

    def on_unmount(self) -> None:
        self._stop_timers()

    # ── Internal ──────────────────────────────────────────────────────────────

    def _show_reason(self, reason: str) -> None:
        try:
            self.query_one("#waiting-reason", Static).update(f"⚠  {reason}")
            self.query_one("#waiting-reason").remove_class("hidden")
        except NoMatches:
            return
        self._reason_timer = self.set_timer(_REASON_DISPLAY_S, self._hide_reason)

    def _hide_reason(self) -> None:
        try:
            self.query_one("#waiting-reason").add_class("hidden")
        except NoMatches:
            pass

    def _spin(self) -> None:
        self._spinner_idx = (self._spinner_idx + 1) % len(_SPINNER_FRAMES)
        try:
            self.query_one("#waiting-spinner", Static).update(
                _SPINNER_FRAMES[self._spinner_idx]
            )
        except NoMatches:
            pass

    def _poll_usb(self) -> None:
        if Flasher.detect_sdp():
            self._stop_timers()
            self.post_message(self.DeviceDetected(AppMode.FLASHING))
        elif Flasher.detect_cdc():
            self._stop_timers()
            self.post_message(self.DeviceDetected(AppMode.DIAGNOSING))

    def _stop_timers(self) -> None:
        if self._detect_timer:
            self._detect_timer.stop()
        if self._spin_timer:
            self._spin_timer.stop()
        if self._reason_timer:
            self._reason_timer.stop()
