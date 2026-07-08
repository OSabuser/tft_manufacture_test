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

import logging
import tomllib
from pathlib import Path
from typing import Optional
from textual import on
from textual.app import ComposeResult
from textual.containers import Center, Horizontal
from textual.css.query import NoMatches
from textual.message import Message
from textual.screen import Screen
from textual.timer import Timer
from textual.widgets import Button, Static

from ..boot_art import LOGO_ART
from ..flasher import Flasher
from ..models import AppMode
from ..widgets import AppFrame

logger = logging.getLogger(__name__)

_DETECT_INTERVAL_S = 1.5
_SPIN_INTERVAL_S = 0.1
_REASON_DISPLAY_S = 4.0
_SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]


def _read_app_version() -> str:
    """
    Прочитать версию service-tui из pyproject.toml.

    Не используем importlib.metadata — проект не устанавливается как пакет
    (tool.uv.package = false), метаданные могут отсутствовать. Читаем файл
    напрямую через tomllib (stdlib, requires-python >= 3.11 уже задан).
    """
    pyproject_path = Path(__file__).resolve().parents[2] / "pyproject.toml"
    try:
        with pyproject_path.open("rb") as f:
            data = tomllib.load(f)
        return data.get("project", {}).get("version", "0.0.0")
    except Exception as exc:
        logger.warning("Не удалось прочитать версию из %s: %s", pyproject_path, exc)
        return "0.0.0"


_APP_VERSION = _read_app_version()


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
            yield Static(f"service_tool  v{_APP_VERSION}", id="waiting-version")
            with Center(id="waiting-logo-row"):
                yield Static(LOGO_ART, id="waiting-logo-art")
            yield Static("", id="waiting-reason", classes="hidden")
            yield Static("Подключите плату индикатора к USB...", id="waiting-hint")
            yield Static(_SPINNER_FRAMES[0], id="waiting-spinner")
            with Horizontal(id="waiting-btn-row"):
                yield Button(
                    "✕ Выйти из приложения", id="waiting-btn-quit", variant="default"
                )

    def on_mount(self) -> None:
        self._detect_timer = self.set_interval(_DETECT_INTERVAL_S, self._poll_usb)
        self._spin_timer = self.set_interval(_SPIN_INTERVAL_S, self._spin)
        if self._disconnect_reason:
            self._show_reason(self._disconnect_reason)

    def on_unmount(self) -> None:
        self._stop_timers()

    # ── Обработчики ───────────────────────────────────────────────────────────

    @on(Button.Pressed, "#waiting-btn-quit")
    def _on_quit_pressed(self) -> None:
        self.app.exit()

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
            self._stop_detect_polling()
            self.post_message(self.DeviceDetected(AppMode.FLASHING))
        elif Flasher.detect_cdc():
            self._stop_detect_polling()
            self.post_message(self.DeviceDetected(AppMode.DIAGNOSING))

    def _stop_detect_polling(self) -> None:
        """
        Остановить опрос USB, но не спиннер.

        Экран остаётся смонтированным ещё некоторое время после детекта —
        ServiceApp подключается к плате и (в режиме диагностики) ждёт M5
        (см. app.py._connect_and_diagnose, ретрай ping в M5Client.connect()
        может занимать секунды). Если остановить спиннер здесь же, экран
        выглядит зависшим на этот промежуток — спиннер должен крутиться до
        фактического переключения экрана (on_unmount).
        """
        if self._detect_timer:
            self._detect_timer.stop()
        try:
            self.query_one("#waiting-hint", Static).update(
                "Плата найдена, подключаемся..."
            )
        except NoMatches:
            pass

    def _stop_timers(self) -> None:
        if self._detect_timer:
            self._detect_timer.stop()
        if self._spin_timer:
            self._spin_timer.stop()
        if self._reason_timer:
            self._reason_timer.stop()
