"""
post_flash.py — экран-промпт после успешной прошивки firmware_test.

Показывается только для FlashTarget.FIRMWARE_TEST (диагностическая прошивка) —
после неё плата должна быть переведена в нормальный режим (BOOT_MOD → GND)
чтобы попасть в DiagScreen. Без этого промпта TUI зацикливался: SDP всё ещё
виден → WaitingScreen снова детектит SDP → снова FlashScreen.

Для PRODUCTION/CUSTOM (либо когда прошивка не удалась) промпт не нужен —
сразу WaitingScreen с тем же общим поведением автодетекта.

Три способа покинуть экран:
    1. Оператор успел сменить BootMode и нажал "Готово" → WaitingScreen
       (далее автодетект сам поймает CDC, если плата уже перезагружена)
    2. Оператор нажал "Выйти" → app.exit()
    3. Таймаут (по умолчанию 40 с) → автоматически WaitingScreen
"""

from __future__ import annotations

from textual.app import ComposeResult
from textual.containers import Center, Horizontal
from textual.message import Message
from textual.screen import Screen
from textual.timer import Timer
from textual.widgets import Button, Label, Static

from ..widgets import AppFrame

_AUTO_TIMEOUT_S = 40
_TICK_S = 1.0


class PostFlashScreen(Screen):
    """
    Промпт смены BootMode после прошивки firmware_test.

    Messages:
        Done()  — оператор подтвердил или истёк таймаут → пора в WaitingScreen
    """

    class Done(Message):
        """Готово к переходу на WaitingScreen."""

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        self._remaining: int = _AUTO_TIMEOUT_S
        self._timer: Timer | None = None

    def compose(self) -> ComposeResult:
        with AppFrame(id="post-flash-frame"):
            with Center(id="post-flash-title-row"):
                yield Label("✅ firmware_test успешно записан", id="post-flash-title")

            with Center(id="post-flash-instruction-row"):
                yield Static(
                    "Переведите плату в нормальный режим:\nBOOT_MOD_1 → GND → Reset",
                    id="post-flash-instruction",
                )

            yield Static("", id="post-flash-countdown")

            with Horizontal(id="post-flash-btn-row"):
                yield Button("✓ Готово", id="post-flash-btn-ok", variant="success")
                yield Button(
                    "✕ Выйти из приложения", id="post-flash-btn-quit", variant="default"
                )

    def on_mount(self) -> None:
        self._update_countdown()
        self._timer = self.set_interval(_TICK_S, self._tick)

    def on_unmount(self) -> None:
        if self._timer is not None:
            self._timer.stop()

    def _tick(self) -> None:
        self._remaining -= 1
        self._update_countdown()
        if self._remaining <= 0:
            self.post_message(self.Done())

    def _update_countdown(self) -> None:
        self.query_one("#post-flash-countdown", Static).update(
            f"Автопереход через: {self._remaining}с"
        )

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "post-flash-btn-ok":
            self.post_message(self.Done())
        elif event.button.id == "post-flash-btn-quit":
            self.app.exit()
