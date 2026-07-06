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
from .models import AppMode, FlashPreset, FlashTarget
from .screens import DiagScreen, FlashScreen, PostFlashScreen, WaitingScreen

logger = logging.getLogger(__name__)

_CDC_VID = int(os.environ.get("SERVICE_CDC_VID", "0x1996"), 16)
_CDC_PID = int(os.environ.get("SERVICE_CDC_PID", "0x00ad"), 16)


class ServiceApp(App):
    """Корневое приложение service-tui."""

    TITLE = "TFT Board Service Tool"
    CSS_PATH = "app.tcss"  # относительно app/app.py → app/app.tcss

    BINDINGS = [
        Binding("ctrl+c", "quit", "Выход", show=True),
        Binding("ctrl+q", "quit", "Выход"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._fw: Optional[FirmwareClient] = None
        self._m5: Optional[M5Client] = None
        # «Липкий» выбор оператора на FlashScreen — переносится на следующую
        # плату в рамках одного запуска TUI (см. FlashPreset docstring).
        # Сбрасывается при перезапуске TUI, не персистится на диск.
        self._last_flash_preset = FlashPreset()

    def on_mount(self) -> None:
        self.push_screen(WaitingScreen())

    # ── Переходы между экранами ───────────────────────────────────────────────

    @on(WaitingScreen.DeviceDetected)
    @on(WaitingScreen.DeviceDetected)
    def _on_device_detected(self, event: WaitingScreen.DeviceDetected) -> None:
        if event.mode == AppMode.FLASHING:
            self.switch_screen(FlashScreen(preset=self._last_flash_preset))
        elif event.mode == AppMode.DIAGNOSING:
            self._connect_and_diagnose()

    @on(FlashScreen.FlashDone)
    def _on_flash_done(self, event: FlashScreen.FlashDone) -> None:
        """
        После прошивки:
          - firmware_test + успех → PostFlashScreen (промпт смены BootMode)
          - production/custom + успех → WaitingScreen
          - target=None — обрыв USB (watcher в простое ИЛИ backend во время
            активной операции, см. models.FlashResult, Фаза 4a вариант 2)
            → WaitingScreen с причиной (конкретный текст, если есть, иначе
            общий fallback)

        Логическая ошибка (плата на месте) сюда не долетает вовсе —
        FlashScreen в этом случае не покидает себя (см. _do_flash/_do_erase).
        """
        if event.preset is not None:
            self._last_flash_preset = event.preset

        if event.target is None and not event.success:
            WaitingScreen(
                disconnect_reason=event.error_message or "Соединение с платой потеряно"
            )
            return

        if event.success and event.target == FlashTarget.FIRMWARE_TEST:
            self.switch_screen(PostFlashScreen())
        else:
            self.switch_screen(WaitingScreen())

    @on(PostFlashScreen.Done)
    def _on_post_flash_done(self) -> None:
        """Оператор подтвердил смену BootMode или истёк таймаут."""
        self.switch_screen(WaitingScreen())

    @on(DiagScreen.DiagDone)
    def _on_diag_done(self, event: DiagScreen.DiagDone) -> None:
        """После диагностики — отключиться, вернуться в Waiting."""
        self._disconnect()
        self.switch_screen(WaitingScreen(disconnect_reason=event.reason))

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

        fw_version = ""
        try:
            fw_version = await self._fw.get_version()
        except Exception as exc:
            logger.warning("get_version failed: %s", exc)

        self.switch_screen(
            DiagScreen(firmware=self._fw, m5=self._m5, fw_version=fw_version)
        )

    def _disconnect(self) -> None:
        if self._fw is not None:
            self.call_later(self._fw.disconnect)
            self._fw = None
        if self._m5 is not None:
            self.call_later(self._m5.disconnect)
            self._m5 = None
