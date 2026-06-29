"""
diag/__init__.py — экран диагностики (режим B).

DiagScreen координирует три виджета:
    TestListPanel   — выбор тестов (левая колонка)
    ResultsPanel    — результаты (правая колонка)
    ConfirmPanel    — confirm_request оператора (нижняя панель)

и Orchestrator — маршрутизатор confirm_request.
"""

from __future__ import annotations

import logging
from typing import Optional

from textual import on, work
from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal
from textual.css.query import NoMatches
from textual.message import Message
from textual.screen import Screen
from textual.widgets import Button, Label, ProgressBar, Static

from ...firmware_client import FirmwareClient
from ...m5_client import M5Client
from ...models import SessionState
from ...orchestrator import Orchestrator, OrchestratorEvent, OrchestratorEventType
from .confirm_panel import ConfirmPanel
from .results import ResultsPanel
from .test_list import TestListPanel

logger = logging.getLogger(__name__)


class DiagScreen(Screen):
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

    def __init__(
        self,
        firmware: FirmwareClient,
        m5: Optional[M5Client] = None,
        **kwargs,
    ) -> None:
        super().__init__(**kwargs)
        self._fw = firmware
        self._m5 = m5
        self._orchestrator = Orchestrator(firmware, m5)
        self._session = SessionState()
        self._running = False

    def compose(self) -> ComposeResult:
        # Шапка
        with Horizontal(id="diag-header"):
            yield Static("fw: —", id="diag-header-fw")
            yield Static("UID: —", id="diag-header-uid")
            yield Static("M5: —", id="diag-header-m5")

        # Рабочая зона: список тестов + результаты
        with Horizontal(id="diag-main"):
            yield TestListPanel(id="diag-test-list")
            yield ResultsPanel(id="diag-results")

        # Прогресс
        yield ProgressBar(id="diag-progress-bar", show_eta=False)
        yield Label("", id="diag-progress-label")

        # Кнопки запуска
        with Horizontal(id="diag-btn-row"):
            yield Button(
                "▶ Запустить выбранные",
                id="diag-btn-run-selected",
                variant="primary",
                disabled=True,
            )
            yield Button(
                "▶▶ Все тесты",
                id="diag-btn-run-all",
                variant="default",
                disabled=True,
            )

        # Панель confirm
        yield ConfirmPanel(id="diag-confirm", classes="hidden")

    def on_mount(self) -> None:
        self._init_session()

    # ── Инициализация сессии ─────────────────────────────────────────────────

    @work(thread=False)
    async def _init_session(self) -> None:
        try:
            tests = await self._fw.list_tests()
            uid = await self._fw.get_uid()
            version = await self._fw.get_version()
        except Exception as exc:
            logger.error("Session init failed: %s", exc)
            self.post_message(self.DiagDone())
            return

        self._session.tests = tests
        self._session.chip_uid = uid
        self._session.fw_version = version
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
        uid_short = self._session.chip_uid[:16] if self._session.chip_uid else "—"
        self.query_one("#diag-header-uid", Static).update(f"UID: {uid_short}")

        m5_widget = self.query_one("#diag-header-m5", Static)
        if self._session.m5_connected:
            m5_widget.update("M5: ✓ подключён")
            m5_widget.remove_class("m5-absent")
        else:
            m5_widget.update("M5: — нет")
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

    def action_go_back(self) -> None:
        if not self._running:
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
        self._update_progress(0, len(test_ids), "")
        self._set_run_buttons(enabled=False)
        self._running = True
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
            logger.error("run_worker error: %s", exc)
        finally:
            self._running = False
            self._set_run_buttons(enabled=True)
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

        elif event.type == OrchestratorEventType.CONFIRM_NEEDED:
            assert event.confirm is not None
            confirm.show_operator(
                prompt=event.confirm.prompt,
                timeout_ms=event.confirm.timeout_ms,
            )

        elif event.type == OrchestratorEventType.CONFIRM_RESOLVED:
            self._update_progress(done, total, f"HIL: {event.message}")

        elif event.type == OrchestratorEventType.BUTTONS_PROMPT:
            assert event.confirm is not None
            confirm.show_buttons_hint(event.confirm.prompt)

        elif event.type == OrchestratorEventType.SUMMARY:
            self._on_summary(event.summary or {})

        elif event.type == OrchestratorEventType.ERROR:
            self._update_progress(done, total, f"⚠ {event.message}")

    def _on_summary(self, summary: dict) -> None:
        overall = summary.get("overall", "fail")
        passed = summary.get("passed", 0)
        failed = summary.get("failed", 0)
        icon = "✅" if overall == "pass" else "❌"
        self.query_one("#diag-progress-label", Label).update(
            f"{icon}  Итог: {passed} прошли, {failed} не прошли"
        )

    # ── Утилиты ───────────────────────────────────────────────────────────────

    def _update_progress(self, done: int, total: int, msg: str) -> None:
        pct = int(done / total * 100) if total else 0
        self.query_one("#diag-progress-bar", ProgressBar).update(progress=pct)
        self.query_one("#diag-progress-label", Label).update(msg)

    def _set_run_buttons(self, enabled: bool) -> None:
        self.query_one("#diag-btn-run-selected", Button).disabled = not enabled
        self.query_one("#diag-btn-run-all", Button).disabled = not enabled
