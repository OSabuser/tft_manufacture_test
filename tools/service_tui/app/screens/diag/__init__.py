"""
diag/__init__.py — экран диагностики (режим B).

DiagScreen координирует три виджета:
    TestListPanel   — выбор тестов (левая колонка)
    ResultsPanel    — результаты (правая колонка)
    ConfirmPanel    — confirm_request оператора (нижняя панель)

и Orchestrator — маршрутизатор confirm_request.

Мониторинг соединения (см. отчёт, замечание №4): пока тесты не запущены,
каждые 1.5с проверяется наличие CDC-порта на шине. Во время активного
прогона мониторинг приостановлен — таймаут чтения порта внутри Orchestrator
уже детектирует обрыв связи надёжнее (видит реальную остановку потока
данных, а не просто исчезновение устройства из списка портов) и сам
формирует понятный результат для прерванного теста.
"""

from __future__ import annotations

import logging
from typing import Optional

from textual import on, work
from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.css.query import NoMatches
from textual.message import Message
from textual.screen import Screen
from textual.widgets import Button, Label, ProgressBar, Static

from ...firmware_client import FirmwareClient
from ...flasher import Flasher
from ...m5_client import M5Client
from ...models import SessionState
from ...orchestrator import Orchestrator, OrchestratorEvent, OrchestratorEventType
from ...widgets import AppFrame
from ..connection_watcher import ConnectionLost, ConnectionWatcherMixin
from .confirm_panel import ConfirmPanel
from .results import ResultsPanel
from .test_list import TestListPanel

logger = logging.getLogger(__name__)


class DiagScreen(Screen, ConnectionWatcherMixin):
    """
    Экран диагностики.

    Messages:
        DiagDone()  — сессия завершена (плата отключена или summary получен)
    """

    BINDINGS = [
        Binding("escape", "go_back", "Отключиться"),
        Binding("r", "run_all", "Все тесты"),
        Binding("s", "run_selected", "Выбранные"),
    ]

    class DiagDone(Message):
        """Сессия диагностики завершена."""

        def __init__(self, reason: Optional[str] = None) -> None:
            super().__init__()
            self.reason = reason

    def __init__(
        self,
        firmware: FirmwareClient,
        m5: Optional[M5Client] = None,
        fw_version: str = "",
        **kwargs,
    ) -> None:
        super().__init__(**kwargs)
        self._fw = firmware
        self._m5 = m5
        self._orchestrator = Orchestrator(firmware, m5)
        self._session = SessionState(fw_version=fw_version)
        self._tests_running = False

    def compose(self) -> ComposeResult:
        with AppFrame(id="diag-frame"):
            # Шапка — фиксированная высота 3
            with Horizontal(id="diag-header"):
                yield Static("fw: —", id="diag-header-fw")
                yield Static("MCU ID: —", id="diag-header-uid")
                yield Static("M5 Bench: —", id="diag-header-m5")

            # Рабочая зона: список тестов + результаты — занимает всё
            # оставшееся место (1fr), сама прокручивается при переполнении
            with Horizontal(id="diag-main"):
                yield TestListPanel(id="diag-test-list")
                yield ResultsPanel(id="diag-results")

            # Прогресс — скрыт пока нет активного прогона. Контейнер
            # имеет classes="hidden" по умолчанию — height: auto + display:
            # none даёт нулевую высоту, не отнимая место у остального layout.
            with Vertical(id="diag-progress-row", classes="hidden"):
                yield ProgressBar(
                    id="diag-progress-bar", show_eta=False, show_percentage=False
                )
                yield Label("", id="diag-progress-label")

            # Кнопки запуска — фиксированная высота 3 (под border Button)
            with Horizontal(id="diag-btn-row"):
                yield Button(
                    "▶ Запустить выбранные тесты",
                    id="diag-btn-run-selected",
                    variant="primary",
                    disabled=True,
                )
                yield Button(
                    "▶▶ Запустить все тесты",
                    id="diag-btn-run-all",
                    variant="default",
                    disabled=True,
                )
                yield Button(
                    "✕ Выйти из приложения", id="diag-btn-quit", variant="default"
                )

            # Панель confirm — auto-высота, видна только когда есть запрос
            yield ConfirmPanel(id="diag-confirm", classes="hidden")

    def on_mount(self) -> None:
        self._init_session()
        self._start_connection_watch(self._check_cdc_present)

    def on_unmount(self) -> None:
        self._stop_connection_watch()

    def _check_cdc_present(self) -> bool:
        # Не считаем потерей соединения во время активного прогона —
        # Orchestrator сам детектирует обрыв через таймаут чтения порта
        # (надёжнее: видит остановку потока данных, не просто список USB).
        if self._tests_running:
            return True
        return Flasher.detect_cdc()

    @on(ConnectionLost)
    def _on_connection_lost(self) -> None:
        self.post_message(self.DiagDone(reason="Соединение с платой потеряно"))

    # ── Инициализация сессии ─────────────────────────────────────────────────

    @work(thread=False)
    async def _init_session(self) -> None:
        try:
            tests = await self._fw.list_tests()
            uid = await self._fw.get_uid()
        except Exception as exc:
            logger.error("Session init failed: %s", exc)
            self.post_message(self.DiagDone(reason="Не удалось получить список тестов"))
            return

        self._session.tests = tests
        self._session.chip_uid = uid
        self._session.m5_connected = self._m5 is not None

        self._update_header()

        test_list = self.query_one("#diag-test-list", TestListPanel)
        test_list.populate(tests, m5_connected=self._session.m5_connected)

        self.query_one("#diag-results", ResultsPanel).populate(tests)
        self._set_run_buttons(enabled=True)

    def _update_header(self) -> None:
        self.query_one("#diag-header-fw", Static).update(
            f"fw: {self._session.fw_version or '?'}"
        )
        uid = self._session.chip_uid or "—"
        self.query_one("#diag-header-uid", Static).update(f"MCU ID: {uid}")

        m5_widget = self.query_one("#diag-header-m5", Static)
        if self._session.m5_connected:
            m5_widget.update("M5 Bench: ✓ подключён")
            m5_widget.remove_class("m5-absent")
        else:
            m5_widget.update("M5 Bench: ✕ нет связи")
            m5_widget.add_class("m5-absent")

    # ── Кнопки ───────────────────────────────────────────────────────────────

    @on(Button.Pressed, "#diag-btn-run-selected")
    def _on_run_selected(self) -> None:
        ids = self.query_one("#diag-test-list", TestListPanel).get_selected_ids()
        if ids:
            self._start_run(ids)

    @on(Button.Pressed, "#diag-btn-run-all")
    def _on_run_all(self) -> None:
        ids = [t.id for t in self._session.tests]
        if ids:
            self._start_run(ids)

    @on(Button.Pressed, "#diag-btn-quit")
    def _on_quit_pressed(self) -> None:
        if not self._tests_running:
            self.app.exit()

    def action_go_back(self) -> None:
        if not self._tests_running:
            self.post_message(self.DiagDone())

    def action_run_all(self) -> None:
        self._on_run_all()

    def action_run_selected(self) -> None:
        self._on_run_selected()

    # ── Confirm ───────────────────────────────────────────────────────────────

    @on(ConfirmPanel.Confirmed)
    def _on_confirmed(self, event: ConfirmPanel.Confirmed) -> None:
        """Оператор ответил — передать в оркестратор."""
        self.app.call_later(
            self._orchestrator.resolve_operator_confirm, event.confirmed
        )

    # ── Запуск тестов ────────────────────────────────────────────────────────

    def _start_run(self, test_ids: list[str]) -> None:
        results = self.query_one("#diag-results", ResultsPanel)
        results.reset()
        self._session.results.clear()
        self._show_progress(True)
        self._update_progress(0, len(test_ids), "")
        self._set_run_buttons(enabled=False)
        self.query_one("#diag-test-list", TestListPanel).set_enabled(False)
        self._tests_running = True
        self._run_worker(test_ids)

    @work(exclusive=True, thread=False)
    async def _run_worker(self, test_ids: list[str]) -> None:
        total = len(test_ids)
        done = 0
        try:
            async for event in self._orchestrator.run_tests(test_ids):
                self._handle_event(event, total, done)
                if event.type == OrchestratorEventType.TEST_RESULT:
                    done += 1
                if event.type == OrchestratorEventType.SUMMARY:
                    break
        except Exception as exc:
            logger.error("run_worker error: %s", exc, exc_info=True)
            self._update_progress(done, total, f"⚠  Ошибка прогона тестов: {exc}")
        finally:
            self._tests_running = False
            self._set_run_buttons(enabled=True)
            self.query_one("#diag-test-list", TestListPanel).set_enabled(True)
            try:
                self.query_one("#diag-confirm", ConfirmPanel).hide()
            except NoMatches:
                pass

    def _handle_event(self, event: OrchestratorEvent, total: int, done: int) -> None:
        results = self.query_one("#diag-results", ResultsPanel)
        confirm = self.query_one("#diag-confirm", ConfirmPanel)

        if event.type == OrchestratorEventType.TEST_BEGIN:
            results.set_running(event.test_id)
            self._update_progress(done, total, f"Тест: {event.test_id}")

        elif event.type == OrchestratorEventType.TEST_RESULT:
            assert event.result is not None
            self._session.set_result(event.result)
            results.set_result(event.result)

        elif event.type == OrchestratorEventType.TEST_PROGRESS:
            # Внутришаговый прогресс долгого теста (сейчас только usd):
            # card_detect, mount, write, read_compare — см. PROTOCOL.md.
            self._update_progress(
                done, total, f"Тест: {event.test_id} — {event.message}"
            )

        elif event.type == OrchestratorEventType.CONFIRM_NEEDED:
            assert event.confirm is not None
            confirm.show_operator(
                prompt=event.confirm.prompt,
                timeout_ms=event.confirm.timeout_ms,
            )

        elif event.type == OrchestratorEventType.CONFIRM_RESOLVED:
            self._update_progress(done, total, f"M5 Bench: {event.message}")

        elif event.type == OrchestratorEventType.BUTTONS_PROMPT:
            assert event.confirm is not None
            confirm.show_buttons_hint(event.confirm.prompt)

        elif event.type == OrchestratorEventType.SUMMARY:
            self._on_summary(event.summary or {})

        elif event.type == OrchestratorEventType.ERROR:
            self._update_progress(done, total, f"⚠ {event.message}")

    def _on_summary(self, summary: dict) -> None:
        aborted = summary.get("aborted", False)
        if aborted:
            self.query_one("#diag-progress-label", Label).update(
                "⚠  Прогон прерван: связь с платой потеряна"
            )
            return

        overall = summary.get("overall", "fail")
        passed = summary.get("passed", 0)
        failed = summary.get("failed", 0)
        icon = "✅" if overall == "pass" else "❌"
        self.query_one("#diag-progress-label", Label).update(
            f"{icon}  Итог: {passed} прошли, {failed} не прошли"
        )

    # ── Утилиты ───────────────────────────────────────────────────────────────

    def _show_progress(self, visible: bool) -> None:
        """Показать/скрыть строку прогресса. Скрыта в простое — без анимации."""
        row = self.query_one("#diag-progress-row")
        bar = self.query_one("#diag-progress-bar", ProgressBar)
        if visible:
            bar.update(total=100, progress=0)
            row.remove_class("hidden")
        else:
            row.add_class("hidden")

    def _update_progress(self, done: int, total: int, msg: str) -> None:
        pct = int(done / total * 100) if total else 0
        self.query_one("#diag-progress-bar", ProgressBar).update(
            total=100, progress=pct
        )
        self.query_one("#diag-progress-label", Label).update(msg)

    def _set_run_buttons(self, enabled: bool) -> None:
        self.query_one("#diag-btn-run-selected", Button).disabled = not enabled
        self.query_one("#diag-btn-run-all", Button).disabled = not enabled
