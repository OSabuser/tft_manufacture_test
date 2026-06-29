"""
results.py — виджет правой колонки DiagScreen.

Отображает строки результатов тестов: id | статус | detail.
Обновляется по событиям от Orchestrator через DiagScreen.

Публичный API:
    ResultsPanel.populate(tests)          — инициализировать пустые строки
    ResultsPanel.set_running(test_id)     — показать "выполняется"
    ResultsPanel.set_result(result)       — показать финальный результат
    ResultsPanel.reset()                  — сбросить все строки
"""

from __future__ import annotations

from textual.app import ComposeResult
from textual.containers import Horizontal
from textual.css.query import NoMatches
from textual.widget import Widget
from textual.widgets import Label, Static

from ...models import TestResult, TestStatus


def _status_display(status: TestStatus) -> tuple[str, str]:
    """Вернуть (текст, css-класс) для статуса."""
    return {
        TestStatus.PASS: ("✓ PASS", "result-status-pass"),
        TestStatus.FAIL: ("✗ FAIL", "result-status-fail"),
        TestStatus.RUNNING: ("…", "result-status-running"),
        TestStatus.SKIP: ("SKIP", "result-status-skip"),
        TestStatus.PENDING: ("", "result-status-skip"),
    }[status]


# CSS-классы статусов — для очистки перед сменой
_STATUS_CLASSES = (
    "result-status-pass",
    "result-status-fail",
    "result-status-running",
    "result-status-skip",
)


class ResultsPanel(Widget):
    """Правая колонка DiagScreen — результаты тестов."""

    DEFAULT_CSS = ""  # стили в diag.tcss

    def compose(self) -> ComposeResult:
        yield Label("Результаты", classes="section-title")

    # ── Public API ────────────────────────────────────────────────────────────

    def populate(self, tests: list) -> None:
        """
        Инициализировать пустые строки результатов.

        :param tests: list[TestInfo]
        """
        for child in list(self.children):
            if not child.has_class("section-title"):
                child.remove()

        for test in tests:
            row = Horizontal(classes="result-row", id=f"result-row-{test.id}")
            id_lbl = Static(test.id, classes="result-id")
            status_lbl = Static(
                "", classes="result-status-skip", id=f"result-status-{test.id}"
            )
            detail_lbl = Static(
                "", classes="result-detail", id=f"result-detail-{test.id}"
            )
            self.mount(row)
            row.mount(id_lbl)
            row.mount(status_lbl)
            row.mount(detail_lbl)

    def set_running(self, test_id: str) -> None:
        """Пометить тест как выполняющийся."""
        self._update(test_id, TestStatus.RUNNING, "")

    def set_result(self, result: TestResult) -> None:
        """Показать финальный результат теста."""
        detail = result.detail if result.status == TestStatus.FAIL else ""
        self._update(result.id, result.status, detail)

    def reset(self) -> None:
        """Сбросить все строки в пустое состояние."""
        for widget in self.query(Static):
            wid = widget.id or ""
            if wid.startswith("result-status-"):
                for cls in _STATUS_CLASSES:
                    widget.remove_class(cls)
                widget.add_class("result-status-skip")
                widget.update("")
            elif wid.startswith("result-detail-"):
                widget.update("")

    # ── Internal ──────────────────────────────────────────────────────────────

    def _update(self, test_id: str, status: TestStatus, detail: str) -> None:
        try:
            status_w = self.query_one(f"#result-status-{test_id}", Static)
            detail_w = self.query_one(f"#result-detail-{test_id}", Static)
        except NoMatches:
            return

        for cls in _STATUS_CLASSES:
            status_w.remove_class(cls)

        text, css = _status_display(status)
        status_w.update(text)
        status_w.add_class(css)
        detail_w.update(detail)
