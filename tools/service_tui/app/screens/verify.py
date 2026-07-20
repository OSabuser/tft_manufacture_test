"""
verify.py — экран Тир-1: живая проверка загрузчика после серийной прошивки.

Показывается только для FlashTarget.PRODUCTION при включённом чек-боксе
«Верификация». После SDP-прошивки плата остаётся в SDP-режиме; чтобы запустить
свежезаписанный загрузчик (и получить smoke/qspi по CDC), оператор обязан
перевести BOOT_MOD_1 → GND + reset — тот же ручной шаг, что в PostFlashScreen,
но здесь за ним следует активная проверка, а не просто возврат в ожидание.

Поток:
  1. Промпт смены BOOT_MOD; поллинг CDC (у загрузчика тот же VID:PID, что у
     firmware_test — намеренно).
  2. CDC поднялся → BootloaderClient: ping (внутри connect) + get_version +
     get_smoke_status + get_qspi_info.
  3. Отчёт pass/fail по полям. «Готово»/«Пропустить» → App вернёт на
     WaitingScreen.

Тир-0 (readback записи) к этому экрану отношения не имеет — он выполняется
всегда внутри самой прошивки (flash_backend), до и независимо от этого экрана.
"""

from __future__ import annotations

import asyncio
import logging
import os
from typing import Optional

from textual import work
from textual.app import ComposeResult
from textual.containers import Center, Horizontal, Vertical
from textual.css.query import NoMatches
from textual.message import Message
from textual.screen import Screen
from textual.timer import Timer
from textual.widgets import Button, Label, Static

from ..bootloader_client import BootloaderClient
from ..flasher import Flasher
from ..widgets import AppFrame

logger = logging.getLogger(__name__)

_CDC_VID = int(os.environ.get("SERVICE_CDC_VID", "0x1996"), 16)
_CDC_PID = int(os.environ.get("SERVICE_CDC_PID", "0x00ad"), 16)

# Время на смену BOOT_MOD + reset + boot загрузчика + USB-энумерацию. Щедро —
# оператор физически переключает пин; лучше не упереться в таймаут на медленной
# руке, чем сэкономить секунды.
_BOOT_WAIT_TIMEOUT_S = 45.0
_POLL_INTERVAL_S = 1.0
_SPIN_INTERVAL_S = 0.1
_SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]


def _mark(ok: bool) -> str:
    return "✅" if ok else "❌"


class VerifyScreen(Screen):
    """Тир-1: живая проверка загрузчика по CDC после серийной прошивки.

    Messages:
        Done()  — вернуться на WaitingScreen (готово/пропустить/таймаут).
    """

    class Done(Message):
        """Проверка завершена (или пропущена) — пора в WaitingScreen."""

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        self._spinner_idx: int = 0
        self._spin_timer: Timer | None = None
        self._status_text: str = (
            "Переведите плату в нормальный режим: BOOT_MOD_1 → GND → Reset"
        )
        self._finished: bool = False

    def compose(self) -> ComposeResult:
        with AppFrame(id="verify-frame"):
            with Center(id="verify-title-row"):
                yield Label("🔎 Верификация загрузчика", id="verify-title")

            with Center(id="verify-instruction-row"):
                yield Static(
                    "После серийной прошивки плата осталась в режиме BootROM.\n"
                    "Чтобы проверить загрузчик, переведите её в нормальный режим:\n"
                    "BOOT_MOD_1 → GND → Reset",
                    id="verify-instruction",
                )

            with Center(id="verify-status-row"):
                yield Static("", id="verify-status")

            yield Vertical(id="verify-results", classes="hidden")

            with Horizontal(id="verify-btn-row"):
                yield Button(
                    "⏭ Пропустить проверку", id="verify-btn-done", variant="default"
                )
                yield Button(
                    "✕ Выйти из приложения", id="verify-btn-quit", variant="default"
                )

    def on_mount(self) -> None:
        self._spin_timer = self.set_interval(_SPIN_INTERVAL_S, self._spin)
        self._run_verify()

    def on_unmount(self) -> None:
        if self._spin_timer is not None:
            self._spin_timer.stop()

    # ── Обработчики ───────────────────────────────────────────────────────────

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "verify-btn-done":
            self.post_message(self.Done())
        elif event.button.id == "verify-btn-quit":
            self.app.exit()

    # ── Внутреннее ────────────────────────────────────────────────────────────

    def _spin(self) -> None:
        if self._finished:
            return
        self._spinner_idx = (self._spinner_idx + 1) % len(_SPINNER_FRAMES)
        try:
            self.query_one("#verify-status", Static).update(
                f"{_SPINNER_FRAMES[self._spinner_idx]}  {self._status_text}"
            )
        except NoMatches:
            pass

    @work(exclusive=True, thread=False)
    async def _run_verify(self) -> None:
        client = await self._wait_and_connect()
        if client is None:
            self._render_no_board()
            return

        version, smoke, qspi = "", None, None
        try:
            version = await client.get_version()
            smoke = await client.get_smoke_status()
            qspi = await client.get_qspi_info()
        except Exception as exc:  # noqa: BLE001 — любой сбой запроса → отчёт с «нет ответа»
            logger.warning("Запрос к загрузчику не удался: %s", exc)
        finally:
            await client.disconnect()

        self._render_result(version, smoke, qspi)

    async def _wait_and_connect(self) -> Optional[BootloaderClient]:
        """Ждать появления CDC загрузчика и подключиться (в пределах таймаута).

        detect_cdc() синхронный и быстрый (скан портов) — зовём напрямую, как
        и остальной детект в проекте. auto_connect может не удаться, если порт
        ещё не готов сразу после энумерации — тогда повторяем на следующей
        итерации поллинга, пока не истечёт дедлайн.
        """
        loop = asyncio.get_running_loop()
        deadline = loop.time() + _BOOT_WAIT_TIMEOUT_S
        while loop.time() < deadline:
            if Flasher.detect_cdc():
                self._status_text = "Плата найдена, проверяю загрузчик..."
                try:
                    return await BootloaderClient.auto_connect(
                        vid=_CDC_VID, pid=_CDC_PID
                    )
                except Exception as exc:  # noqa: BLE001 — порт не готов → ретрай
                    logger.info("Подключение к загрузчику, повтор: %s", exc)
            await asyncio.sleep(_POLL_INTERVAL_S)
        return None

    def _render_no_board(self) -> None:
        """Загрузчик не поднялся за отведённое время."""
        self._finish()
        try:
            self.query_one("#verify-status", Static).update(
                f"⚠  Загрузчик не ответил за {int(_BOOT_WAIT_TIMEOUT_S)}с. "
                "Проверьте BOOT_MOD и подключение USB."
            )
            # Дальше нечего пропускать — было ожидание, не активная проверка.
            self.query_one("#verify-btn-done", Button).label = "✓ Готово"
        except NoMatches:
            pass

    def _render_result(
        self, version: str, smoke: Optional[bool], qspi: Optional[dict]
    ) -> None:
        self._finish()

        ok_version = bool(version)
        ok_smoke = smoke is True
        ok_qspi = qspi is not None and bool(qspi.get("pass"))
        overall = ok_version and ok_smoke and ok_qspi

        lines: list[str] = []
        lines.append(
            f"{_mark(ok_version)} Загрузчик отвечает"
            + (f": v{version}" if version else ": нет ответа")
        )
        if smoke is True:
            lines.append("✅ SDRAM smoke-test: пройден")
        elif smoke is False:
            lines.append("❌ SDRAM smoke-test: провал")
        else:
            lines.append("⚠ SDRAM smoke-test: нет ответа")
        if qspi is None:
            lines.append("⚠ QSPI-чип: нет ответа")
        else:
            chip = qspi.get("chip", "?")
            size_mb = qspi.get("size_mb", "?")
            lines.append(f"{_mark(ok_qspi)} QSPI-чип: {chip} ({size_mb} МБ)")

        try:
            status = self.query_one("#verify-status", Static)
            if overall:
                status.update("✅ Верификация пройдена")
                status.remove_class("verify-fail")
                status.add_class("verify-pass")
            else:
                status.update("❌ Верификация НЕ пройдена")
                status.remove_class("verify-pass")
                status.add_class("verify-fail")

            results = self.query_one("#verify-results", Vertical)
            results.remove_children()
            for line in lines:
                results.mount(Static(line, classes="verify-result-line"))
            results.remove_class("hidden")

            self.query_one("#verify-btn-done", Button).label = "✓ Готово"
        except NoMatches:
            pass

    def _finish(self) -> None:
        """Остановить спиннер и зафиксировать экран в терминальном состоянии."""
        self._finished = True
        if self._spin_timer is not None:
            self._spin_timer.stop()
