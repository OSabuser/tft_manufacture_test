"""
flash.py — экран прошивки (режим A).

Активен когда обнаружен BootROM SDP (1FC9:0130).
Поддерживает три варианта прошивки и chip erase.
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Optional

from textual import on, work
from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.css.query import NoMatches
from textual.message import Message
from textual.screen import Screen
from textual.widgets import (
    Button,
    Input,
    Label,
    Log,
    ProgressBar,
    RadioButton,
    RadioSet,
)

from ..flasher import Flasher
from ..models import FlashProgress, FlashTarget

logger = logging.getLogger(__name__)


class FlashScreen(Screen):
    """
    Экран прошивки.

    Messages:
        FlashDone(success)  — прошивка завершена
    """

    BINDINGS = [
        Binding("escape", "go_back", "Назад"),
    ]

    class FlashDone(Message):
        def __init__(self, success: bool) -> None:
            super().__init__()
            self.success = success

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        self._flasher = Flasher()
        self._flashing = False

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label(
                "⚡ Прошивка платы — BootROM SDP обнаружен",
                id="flash-title",
            )

            with Vertical(id="flash-target-group"):
                yield Label("Что прошить?", classes="section-title")
                with RadioSet(id="flash-radio"):
                    yield RadioButton(
                        "firmware_test  (диагностическая прошивка)",
                        id="radio-fw-test",
                        value=True,
                    )
                    yield RadioButton(
                        "Production     (bootloader + tft_app)",
                        id="radio-production",
                    )
                    yield RadioButton(
                        "Кастомный бинарь...",
                        id="radio-custom",
                    )
                with Horizontal(id="flash-custom-path", classes="hidden"):
                    yield Input(
                        placeholder="Путь к HAB-бинарю (.bin)",
                        id="flash-custom-input",
                    )

            with Horizontal(id="flash-btn-row"):
                yield Button(
                    "▶ Прошить",
                    id="flash-btn-flash",
                    variant="warning",
                )
                yield Button(
                    "⚠ Chip Erase",
                    id="flash-btn-erase",
                    variant="error",
                )

            yield ProgressBar(id="flash-progress-bar", show_eta=False)
            yield Label("", id="flash-progress-label")
            yield Log(id="flash-log", auto_scroll=True)

    # ── Обработчики ───────────────────────────────────────────────────────────

    @on(RadioSet.Changed, "#flash-radio")
    def _on_radio_changed(self, event: RadioSet.Changed) -> None:
        is_custom = event.pressed.id == "radio-custom"
        path_row = self.query_one("#flash-custom-path")
        if is_custom:
            path_row.remove_class("hidden")
        else:
            path_row.add_class("hidden")

    @on(Button.Pressed, "#flash-btn-flash")
    def _on_flash_pressed(self) -> None:
        if self._flashing:
            return
        target, bin_path = self._resolve_target()
        if target is None:
            self._log("⚠ Укажите путь к бинарю")
            return
        self._do_flash(target, bin_path)

    @on(Button.Pressed, "#flash-btn-erase")
    def _on_erase_pressed(self) -> None:
        if self._flashing:
            return
        self._do_erase()

    def action_go_back(self) -> None:
        if not self._flashing:
            self.post_message(self.FlashDone(success=False))

    # ── Workers ───────────────────────────────────────────────────────────────

    @work(exclusive=True, thread=False)
    async def _do_flash(self, target: FlashTarget, bin_path: Optional[Path]) -> None:
        self._set_busy(True)
        self._log(f"▶ Прошивка: {target.value}")
        ok = await self._flasher.flash(
            target=target,
            bin_path=bin_path,
            progress_cb=self._on_progress,
        )
        self._set_busy(False)
        self._log("✅ Готово" if ok else "❌ Ошибка")
        self.post_message(self.FlashDone(success=ok))

    @work(exclusive=True, thread=False)
    async def _do_erase(self) -> None:
        self._set_busy(True)
        self._log("⚠ Chip erase (~30 с)...")
        ok = await self._flasher.erase_chip(progress_cb=self._on_progress)
        self._set_busy(False)
        self._log("✅ Chip erase завершён" if ok else "❌ Chip erase: ошибка")

    # ── Вспомогательные ───────────────────────────────────────────────────────

    def _resolve_target(self) -> tuple[Optional[FlashTarget], Optional[Path]]:
        radio = self.query_one("#flash-radio", RadioSet)
        pressed_id = radio.pressed_button.id if radio.pressed_button else None

        if pressed_id == "radio-fw-test":
            return FlashTarget.FIRMWARE_TEST, None
        if pressed_id == "radio-production":
            return FlashTarget.PRODUCTION, None
        if pressed_id == "radio-custom":
            raw = self.query_one("#flash-custom-input", Input).value.strip()
            if not raw:
                return None, None
            p = Path(raw)
            if not p.exists():
                self._log(f"⚠ Файл не найден: {p}")
                return None, None
            return FlashTarget.CUSTOM, p
        return None, None

    async def _on_progress(self, progress: FlashProgress) -> None:
        self.query_one("#flash-progress-bar", ProgressBar).update(
            progress=progress.percent
        )
        self.query_one("#flash-progress-label", Label).update(
            f"{progress.phase}  {progress.percent}%"
        )
        self._log(progress.message)

    def _set_busy(self, busy: bool) -> None:
        self._flashing = busy
        self.query_one("#flash-btn-flash", Button).disabled = busy
        self.query_one("#flash-btn-erase", Button).disabled = busy

    def _log(self, msg: str) -> None:
        try:
            self.query_one("#flash-log", Log).write_line(msg)
        except NoMatches:
            pass
