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

from textual import on
from textual.app import ComposeResult
from textual.containers import Horizontal
from textual.widget import Widget
from textual.widgets import Button, Checkbox, Label


class TestListPanel(Widget):
    """Левая колонка DiagScreen — список тестов с чекбоксами."""

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        # test_id → Checkbox для быстрого доступа
        self._checkboxes: dict[str, Checkbox] = {}
        # test_id → True если HIL-тест недоступен без M5 (постоянное состояние,
        # не зависящее от прогона). Отдельно от Checkbox.disabled, который
        # временно перещёлкивается на время прогона тестов через set_enabled().
        self._hil_unavailable: dict[str, bool] = {}

    def compose(self) -> ComposeResult:
        yield Label("Тесты", classes="section-title")
        with Horizontal(id="test-list-select-row"):
            yield Button(
                "Выбрать все", id="test-list-select-all", classes="-textual-compact"
            )
            yield Button(
                "Снять все", id="test-list-select-none", classes="-textual-compact"
            )

    # ── Public API ────────────────────────────────────────────────────────────

    def populate(self, tests: list, m5_connected: bool) -> None:
        """
        Заполнить список тестами. Изначально все тесты НЕ выбраны
        (сервисник выбирает явно что запускать) — кроме HIL без M5,
        которые всегда disabled.

        :param tests:        list[TestInfo]
        :param m5_connected: True если M5StampPLC подключён
        """
        # Удалить старые строки (кроме заголовка и select-row)
        for child in list(self.children):
            if child.id != "test-list-select-row" and not child.has_class(
                "section-title"
            ):
                child.remove()
        self._checkboxes.clear()
        self._hil_unavailable.clear()

        rows = []
        for test in tests:
            hil_unavailable = test.requires_hil and not m5_connected
            self._hil_unavailable[test.id] = hil_unavailable

            cb = Checkbox(
                test.name,
                value=False,
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

            rows.append(
                Horizontal(
                    cb,
                    Label(badge_text, classes=hil_css),
                    classes="test-row",
                )
            )

        if rows:
            self.mount_all(rows)

    def get_selected_ids(self) -> list[str]:
        """Вернуть список id выбранных (checked + not disabled) тестов."""
        return [
            tid for tid, cb in self._checkboxes.items() if cb.value and not cb.disabled
        ]

    def set_enabled(self, enabled: bool) -> None:
        """
        Разрешить/запретить изменение чекбоксов во время прогона.

        HIL-чекбоксы без M5 остаются disabled всегда — их постоянное
        состояние хранится в self._hil_unavailable и не зависит от прогона.
        """
        for tid, cb in self._checkboxes.items():
            if self._hil_unavailable.get(tid, False):
                cb.disabled = True  # HIL без M5 — всегда недоступен
            else:
                cb.disabled = not enabled

        self.query_one("#test-list-select-all", Button).disabled = not enabled
        self.query_one("#test-list-select-none", Button).disabled = not enabled

    # ── Обработчики ───────────────────────────────────────────────────────────

    @on(Button.Pressed, "#test-list-select-all")
    def _on_select_all(self) -> None:
        for tid, cb in self._checkboxes.items():
            if not self._hil_unavailable.get(tid, False):
                cb.value = True

    @on(Button.Pressed, "#test-list-select-none")
    def _on_select_none(self) -> None:
        for tid, cb in self._checkboxes.items():
            if not self._hil_unavailable.get(tid, False):
                cb.value = False
