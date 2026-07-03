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
    Label,
    Log,
    ProgressBar,
    RadioButton,
    RadioSet,
    Select,
    Switch,
)

from ..flasher import CUSTOM_BINARIES_DIR, Flasher
from ..models import FcbVariant, FlashPreset, FlashProgress, FlashTarget
from ..widgets import AppFrame
from .connection_watcher import ConnectionLost, ConnectionWatcherMixin

logger = logging.getLogger(__name__)


class FlashScreen(Screen, ConnectionWatcherMixin):
    """
    Экран прошивки.

    Messages:
        FlashDone(success, target)  — прошивка завершена

    Мониторинг соединения: пока плата не прошивается (self._flashing
    == False), каждые 1.5с проверяется наличие BootROM SDP на шине.
    Если плата физически отключена в простое — сессия считается
    недостоверной, экран сразу уходит на WaitingScreen (см. замечание
    №4 отчёта). Во время самой прошивки/erase мониторинг приостановлен —
    обрыв в этом случае обнаруживает сам flash_backend (SPSDKConnectionError
    → ConnectionLostError, см. Фазу 4) и репортит через #flash-log.
    """

    BINDINGS = [
        Binding("escape", "go_back", "Назад"),
    ]

    class FlashDone(Message):
        def __init__(
            self,
            success: bool,
            target: Optional[FlashTarget] = None,
            preset: Optional[FlashPreset] = None,
        ) -> None:
            super().__init__()
            self.success = success
            self.target = target
            self.preset = preset

    def __init__(self, preset: Optional[FlashPreset] = None, **kwargs) -> None:
        super().__init__(**kwargs)
        self._flasher = Flasher()
        self._flashing = False
        self._preset = preset or FlashPreset()

    def compose(self) -> ComposeResult:
        with AppFrame(id="flash-frame"):
            yield Label(
                "⚡ Загрузка прошивки на плату индикатора (режим BootROM)",
                id="flash-title",
            )

            with Vertical(id="flash-target-group"):
                yield Label("Выбор загружаемой прошивки", classes="section-title")
                with RadioSet(id="flash-radio"):
                    yield RadioButton(
                        "Диагностическая прошивка (firmware_test)",
                        id="radio-fw-test",
                        value=self._preset.target == FlashTarget.FIRMWARE_TEST,
                    )
                    yield RadioButton(
                        "Серийная прошивка (bootloader + tft_app)",
                        id="radio-production",
                        value=self._preset.target == FlashTarget.PRODUCTION,
                    )
                    yield RadioButton(
                        "Другое",
                        id="radio-custom",
                        value=self._preset.target == FlashTarget.CUSTOM,
                    )
                is_custom = self._preset.target == FlashTarget.CUSTOM
                with Vertical(
                    id="flash-custom-group",
                    classes="" if is_custom else "hidden",
                ):
                    yield Label("Файл (custom_binaries/)", classes="section-title")
                    yield Select[str](
                        [], id="flash-custom-select", prompt="Выберите файл..."
                    )
                    yield Label("Память платы", classes="section-title")
                    yield Select[str](
                        [(v.display_name, v.value) for v in FcbVariant],
                        id="flash-fcb-select",
                        value=self._preset.fcb_variant.value,
                        allow_blank=False,
                    )
                    with Horizontal(id="flash-dcd-row"):
                        yield Switch(value=self._preset.use_dcd, id="flash-dcd-switch")
                        yield Label("Использует SDRAM (DCD)", classes="section-title")

            with Horizontal(id="flash-btn-row"):
                yield Button("▶ Загрузить", id="flash-btn-flash", variant="warning")
                yield Button("⚠ Очистить память", id="flash-btn-erase", variant="error")
                yield Button(
                    "✕ Выйти из приложения", id="flash-btn-quit", variant="default"
                )

            yield ProgressBar(
                id="flash-progress-bar",
                show_eta=False,
                show_percentage=False,
                classes="hidden",
            )
            yield Log(id="flash-log", auto_scroll=True)

    def on_mount(self) -> None:
        self._start_connection_watch(self._check_sdp_present)
        self._populate_custom_select()

    def _populate_custom_select(self) -> None:
        select = self.query_one("#flash-custom-select", Select)
        names = [p.name for p in Flasher.list_custom_binaries()]
        select.set_options([(name, name) for name in names])
        if not names:
            self._log(f"⚠ Пусто: {CUSTOM_BINARIES_DIR}")
            return
        if self._preset.custom_bin_name in names:
            select.value = self._preset.custom_bin_name

    def on_unmount(self) -> None:
        self._stop_connection_watch()

    def _check_sdp_present(self) -> bool:
        # Не считаем потерей соединения, если идёт активная операция —
        # flash_usb.py сам обработает реальный обрыв через subprocess.
        # обрыв в этом случае обнаружит и обработает сам flash_backend
        # (ConnectionLostError, см. Фазу 4), не watcher.
        if self._flashing:
            return True
        return Flasher.detect_sdp()

    @on(ConnectionLost)
    def _on_connection_lost(self) -> None:
        self.post_message(self.FlashDone(success=False, target=None))

    # ── Обработчики ───────────────────────────────────────────────────────────

    @on(RadioSet.Changed, "#flash-radio")
    def _on_radio_changed(self, event: RadioSet.Changed) -> None:
        is_custom = event.pressed.id == "radio-custom"
        group = self.query_one("#flash-custom-group")
        if is_custom:
            group.remove_class("hidden")
        else:
            group.add_class("hidden")

    @on(Button.Pressed, "#flash-btn-flash")
    def _on_flash_pressed(self) -> None:
        if self._flashing:
            return
        target, bin_path = self._resolve_target()
        if target is None:
            self._log("⚠ Выберите файл в custom_binaries/")
            return

        if target == FlashTarget.CUSTOM:
            preset = FlashPreset(
                target=target,
                custom_bin_name=bin_path.name,
                use_dcd=self._current_use_dcd(),
                fcb_variant=self._current_fcb_variant(),
            )
        else:
            preset = FlashPreset(target=target)

        self._do_flash(target, bin_path, preset)

    @on(Button.Pressed, "#flash-btn-erase")
    def _on_erase_pressed(self) -> None:
        if not self._flashing:
            self._do_erase()

    @on(Button.Pressed, "#flash-btn-quit")
    def _on_quit_pressed(self) -> None:
        if not self._flashing:
            self.app.exit()

    def action_go_back(self) -> None:
        if not self._flashing:
            self.post_message(self.FlashDone(success=False))

    # ── Workers ───────────────────────────────────────────────────────────────

    @work(exclusive=True, thread=False)
    async def _do_flash(
        self, target: FlashTarget, bin_path: Optional[Path], preset: FlashPreset
    ) -> None:
        self._set_busy(True)
        self._show_progress(True)
        self._log(f"▶ Прошивка: {target.value}")
        ok = await self._flasher.flash(
            target=target,
            bin_path=bin_path,
            use_dcd=preset.use_dcd,
            fcb_variant=preset.fcb_variant,
            progress_cb=self._on_progress,
        )
        self._set_busy(False)
        self._finish_progress(ok)
        self._log("✅ Готово" if ok else "❌ Ошибка")
        self.post_message(self.FlashDone(success=ok, target=target, preset=preset))

    @work(exclusive=True, thread=False)
    async def _do_erase(self) -> None:
        self._set_busy(True)
        self._show_progress(True)
        self._log("⚠ Очистка памяти (~30 с)...")
        ok = await self._flasher.erase_chip(progress_cb=self._on_progress)
        self._set_busy(False)
        self._finish_progress(ok)
        self._log("✅ Очистка памяти завершена" if ok else "❌ Очистка памяти: ошибка")

    # ── Вспомогательные ───────────────────────────────────────────────────────

    def _resolve_target(self) -> tuple[Optional[FlashTarget], Optional[Path]]:
        radio = self.query_one("#flash-radio", RadioSet)
        pressed_id = radio.pressed_button.id if radio.pressed_button else None

        if pressed_id == "radio-fw-test":
            return FlashTarget.FIRMWARE_TEST, None
        if pressed_id == "radio-production":
            return FlashTarget.PRODUCTION, None
        if pressed_id == "radio-custom":
            name = self.query_one("#flash-custom-select", Select).value
            if name is None or name is Select.BLANK:
                return None, None
            p = CUSTOM_BINARIES_DIR / str(name)
            if not p.exists():
                self._log(f"⚠ Файл не найден: {p}")
                return None, None
            return FlashTarget.CUSTOM, p
        return None, None

    def _current_fcb_variant(self) -> FcbVariant:
        return FcbVariant(self.query_one("#flash-fcb-select", Select).value)

    def _current_use_dcd(self) -> bool:
        return self.query_one("#flash-dcd-switch", Switch).value

    async def _on_progress(self, progress: FlashProgress) -> None:
        bar = self.query_one("#flash-progress-bar", ProgressBar)
        bar.update(total=100, progress=progress.percent)
        self._log(progress.message)

    def _show_progress(self, visible: bool) -> None:
        """Показать/скрыть прогресс-бар. Скрыт в простое — без анимации."""
        bar = self.query_one("#flash-progress-bar", ProgressBar)
        if visible:
            bar.update(total=100, progress=0)
            bar.remove_class("hidden")
        else:
            bar.add_class("hidden")

    def _finish_progress(self, success: bool) -> None:
        """Зафиксировать прогресс-бар на 100% с финальным статусом."""
        bar = self.query_one("#flash-progress-bar", ProgressBar)
        bar.update(total=100, progress=100)

    def _set_busy(self, busy: bool) -> None:
        self._flashing = busy
        self.query_one("#flash-btn-flash", Button).disabled = busy
        self.query_one("#flash-btn-erase", Button).disabled = busy
        self.query_one("#flash-btn-quit", Button).disabled = busy

    def _log(self, msg: str) -> None:
        try:
            self.query_one("#flash-log", Log).write_line(msg)
        except NoMatches:
            pass
