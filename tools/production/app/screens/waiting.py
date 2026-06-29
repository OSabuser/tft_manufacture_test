"""
waiting.py — экран ожидания подключения платы.

Опрашивает USB каждые _DETECT_INTERVAL_S секунд через Flasher.
При обнаружении SDP или CDC отправляет DeviceDetected message в App.
"""

from __future__ import annotations

from textual.app import ComposeResult
from textual.css.query import NoMatches
from textual.screen import Screen
from textual.timer import Timer
from textual.widgets import Static
from textual.message import Message
from ..flasher import Flasher
from ..models import AppMode

_DETECT_INTERVAL_S = 1.5
_SPIN_INTERVAL_S = 0.1
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

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        self._spinner_idx: int = 0
        self._detect_timer: Timer | None = None
        self._spin_timer: Timer | None = None

    def compose(self) -> ComposeResult:
        yield Static("TFT Indicator Board\nService Tool", id="waiting-logo")
        yield Static("Подключите плату к USB...", id="waiting-hint")
        yield Static(_SPINNER_FRAMES[0], id="waiting-spinner")

    def on_mount(self) -> None:
        self._detect_timer = self.set_interval(_DETECT_INTERVAL_S, self._poll_usb)
        self._spin_timer = self.set_interval(_SPIN_INTERVAL_S, self._spin)

    def on_unmount(self) -> None:
        if self._detect_timer:
            self._detect_timer.stop()
        if self._spin_timer:
            self._spin_timer.stop()

    # ── Internal ──────────────────────────────────────────────────────────────

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
