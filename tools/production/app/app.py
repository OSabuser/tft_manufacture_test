"""
app.py — корневое Textual приложение.

Управляет сменой экранов и жизненным циклом клиентов
(FirmwareClient, M5Client).
"""

from __future__ import annotations

import logging
import os
from typing import Optional

from textual import on, work
from textual.app import App
from textual.binding import Binding

from .firmware_client import FirmwareClient
from .m5_client import M5Client
from .models import AppMode
from .screens import DiagScreen, FlashScreen, WaitingScreen

logger = logging.getLogger(__name__)

_CDC_VID = int(os.environ.get("SERVICE_CDC_VID", "0x1996"), 16)
_CDC_PID = int(os.environ.get("SERVICE_CDC_PID", "0x00ad"), 16)


class ServiceApp(App):
    """Корневое приложение service-tui."""

    TITLE = "TFT Board Service Tool"
    BINDINGS = [
        Binding("ctrl+c", "quit", "Выход", show=True),
        Binding("ctrl+q", "quit", "Выход"),
    ]
    CSS_PATH = "app.tcss"

    def __init__(self) -> None:
        super().__init__()
        self._fw: Optional[FirmwareClient] = None
        self._m5: Optional[M5Client] = None

    def on_mount(self) -> None:
        self.push_screen(WaitingScreen())

    # ── Переходы между экранами ───────────────────────────────────────────────

    @on(WaitingScreen.DeviceDetected)
    def _on_device_detected(self, event: WaitingScreen.DeviceDetected) -> None:
        if event.mode == AppMode.FLASHING:
            self.switch_screen(FlashScreen())
        elif event.mode == AppMode.DIAGNOSING:
            self._connect_and_diagnose()

    @on(FlashScreen.FlashDone)
    def _on_flash_done(self, _event: FlashScreen.FlashDone) -> None:
        """После прошивки — вернуться на WaitingScreen."""
        self.switch_screen(WaitingScreen())

    @on(DiagScreen.DiagDone)
    def _on_diag_done(self) -> None:
        """После диагностики — отключиться, вернуться в Waiting."""
        self._disconnect()
        self.switch_screen(WaitingScreen())

    # ── Подключение к firmware_test ───────────────────────────────────────────

    @work(thread=False)
    async def _connect_and_diagnose(self) -> None:
        try:
            self._fw = await FirmwareClient.auto_connect(vid=_CDC_VID, pid=_CDC_PID)
        except Exception as exc:
            logger.error("CDC connect failed: %s", exc)
            self.switch_screen(WaitingScreen())
            return

        self._m5 = await M5Client.auto_connect()
        self.switch_screen(DiagScreen(firmware=self._fw, m5=self._m5))

    def _disconnect(self) -> None:
        if self._fw is not None:
            self.call_later(self._fw.disconnect)
            self._fw = None
        if self._m5 is not None:
            self.call_later(self._m5.disconnect)
            self._m5 = None
