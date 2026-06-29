"""
test_list.py — виджет списка тестов с чекбоксами.

Отображает тесты из list_tests, отмечает HIL-тесты,
серит недоступные (HIL без M5).

Публичный API:
    TestListPanel.populate(tests, m5_connected)  — заполнить список
    TestListPanel.get_selected_ids()             — список выбранных id
    TestListPanel.set_enabled(enabled)           — блокировать во время прогона
"""

from __future__ import annotations

from textual.app import ComposeResult
from textual.containers import Horizontal
from textual.widget import Widget
from textual.widgets import Checkbox, Label


class TestListPanel(Widget):
    """Левая колонка DiagScreen — список тестов с чекбоксами."""

    DEFAULT_CSS = ""  # стили в diag.tcss

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        # test_id → Checkbox для быстрого доступа
        self._checkboxes: dict[str, Checkbox] = {}

    def compose(self) -> ComposeResult:
        yield Label("Тесты", classes="section-title")

    # ── Public API ────────────────────────────────────────────────────────────

    def populate(self, tests: list, m5_connected: bool) -> None:
        """
        Заполнить список тестами.

        :param tests:        list[TestInfo]
        :param m5_connected: True если M5StampPLC подключён
        """
        # Удалить старые строки (кроме заголовка)
        for child in list(self.children):
            if not child.has_class("section-title"):
                child.remove()
        self._checkboxes.clear()

        for test in tests:
            hil_unavailable = test.requires_hil and not m5_connected

            cb = Checkbox(
                test.name,
                value=not hil_unavailable,
                disabled=hil_unavailable,
                id=f"cb-{test.id}",
                classes="-textual-compact",
            )
            self._checkboxes[test.id] = cb

            hil_css = (
                "test-row-hil-badge hil-disabled"
                if hil_unavailable
                else "test-row-hil-badge"
            )
            badge_text = "[HIL]" if test.requires_hil else ""

            row = Horizontal(classes="test-row")
            self.mount(row)
            row.mount(cb)
            row.mount(Label(badge_text, classes=hil_css))

    def get_selected_ids(self) -> list[str]:
        """Вернуть список id выбранных (checked + not disabled) тестов."""
        return [
            tid for tid, cb in self._checkboxes.items() if cb.value and not cb.disabled
        ]

    def set_enabled(self, enabled: bool) -> None:
        """Разрешить/запретить изменение чекбоксов во время прогона."""
        for cb in self._checkboxes.values():
            if not cb.disabled:  # не трогать серые HIL-без-M5
                cb.disabled = not enabled
