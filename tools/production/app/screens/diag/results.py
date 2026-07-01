"""
results.py — виджет правой колонки DiagScreen.

DataTable с колонками: Тест | HIL | Статус | Время | Детали.
До первого запуска показывает empty-state placeholder вместо таблицы.

Сортировка: FAIL всегда наверху (естественно бросается в глаза),
внутри групп статусов — исходный порядок реестра firmware_test.
Строка с FAIL дополнительно подсвечивается красным фоном целиком.

Колонка "Детали" переносит длинный текст на несколько строк внутри ячейки
(а не обрезает) — высота строки для FAIL вычисляется по длине detail
относительно ширины колонки. Остальные статусы всегда однострочные.

Публичный API:
    ResultsPanel.populate(tests)          — показать пустую таблицу (PENDING)
    ResultsPanel.set_running(test_id)     — пометить тест как выполняющийся
    ResultsPanel.set_result(result)       — показать финальный результат
    ResultsPanel.reset()                  — сбросить все строки в PENDING
"""

from __future__ import annotations

import math

from rich.style import Style
from rich.text import Text
from textual.app import ComposeResult
from textual.containers import Center, Middle
from textual.widget import Widget
from textual.widgets import DataTable, Static

from ...models import TestInfo, TestResult, TestStatus

# Порядок сортировки: чем меньше число — тем выше строка в таблице.
# FAIL всегда наверху, PASS/SKIP внизу — внутри групп сохраняется
# исходный порядок реестра (стабильная сортировка).
_SORT_RANK: dict[TestStatus, int] = {
    TestStatus.FAIL: 0,
    TestStatus.RUNNING: 1,
    TestStatus.PENDING: 2,
    TestStatus.SKIP: 3,
    TestStatus.PASS: 4,
}

_STATUS_DISPLAY: dict[TestStatus, tuple[str, str]] = {
    TestStatus.PASS: ("✓ PASS", "green"),
    TestStatus.FAIL: ("✗ FAIL", "bold white"),
    TestStatus.RUNNING: ("…", "yellow"),
    TestStatus.SKIP: ("SKIP", "grey50"),
    TestStatus.PENDING: ("", "grey50"),
}

_FAIL_BG = "dark_red"

# Ширина колонки "Детали" в символах — должна совпадать с шириной,
# заданной явно в on_mount() через table.add_column("Детали", width=...).
# Используется для расчёта высоты строки под перенос текста.
_DETAIL_COL_WIDTH = 21
_MAX_ROW_HEIGHT = 4  # не даём одной FAIL-строке занять весь экран


class ResultsPanel(Widget):
    """Правая колонка DiagScreen — таблица результатов тестов."""

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        # test_id → TestInfo, нужно для перестроения строк при сортировке
        self._tests: dict[str, TestInfo] = {}
        # test_id → текущий статус (для сортировки и повторных update)
        self._statuses: dict[str, TestStatus] = {}
        self._order: list[str] = []  # исходный порядок реестра

    def compose(self) -> ComposeResult:
        with Center(id="results-empty"):
            with Middle():
                yield Static(
                    "Выберите тесты и нажмите\n«Запустить выбранные»",
                    id="results-empty-text",
                )
        yield DataTable(id="results-table", cursor_type="row", classes="hidden")

    def on_mount(self) -> None:
        table = self.query_one("#results-table", DataTable)
        # add_columns() (множественное число) не принимает width — без явной
        # ширины колонка "Детали" сжимается до длины заголовка ("Детали" = 6
        # символов) и обрезает любой более длинный текст, даже однострочный.
        # Используем add_column() по одной с явной шириной под каждую,
        # рассчитанной под ResultsPanel { width: 70 } (см. app.tcss).
        table.add_column("Тест", width=22)
        table.add_column("M5 Bench", width=8)
        table.add_column("Статус", width=8)
        table.add_column("Время", width=6)
        table.add_column("Детали", width=21)

    # ── Public API ────────────────────────────────────────────────────────────

    def populate(self, tests: list[TestInfo]) -> None:
        """
        Заполнить таблицу тестами в состоянии PENDING.
        До первого вызова populate() с непустым списком виден empty-state.
        """
        table = self.query_one("#results-table", DataTable)
        table.clear()
        self._tests.clear()
        self._statuses.clear()
        self._order = [t.id for t in tests]

        if not tests:
            self._show_empty(True)
            return

        for test in tests:
            self._tests[test.id] = test
            self._statuses[test.id] = TestStatus.PENDING
            self._add_row(test, TestStatus.PENDING, duration_ms=0, detail="")

        self._show_empty(False)

    def set_running(self, test_id: str) -> None:
        """Пометить тест как выполняющийся."""
        self._update_row(test_id, TestStatus.RUNNING, duration_ms=0, detail="")

    def set_result(self, result: TestResult) -> None:
        """Показать финальный результат теста."""
        detail = result.detail if result.status == TestStatus.FAIL else ""
        self._update_row(result.id, result.status, result.duration_ms, detail)

    def reset(self) -> None:
        """Сбросить все строки в PENDING (перед повторным запуском)."""
        for test_id in list(self._tests.keys()):
            self._update_row(test_id, TestStatus.PENDING, duration_ms=0, detail="")

    # ── Internal: empty state ───────────────────────────────────────────────

    def _show_empty(self, visible: bool) -> None:
        empty = self.query_one("#results-empty")
        table = self.query_one("#results-table", DataTable)
        if visible:
            empty.remove_class("hidden")
            table.add_class("hidden")
        else:
            empty.add_class("hidden")
            table.remove_class("hidden")

    # ── Internal: строки таблицы ────────────────────────────────────────────

    def _row_height(self, detail: str) -> int:
        """
        Сколько строк нужно ячейке "Детали" под перенос текста.
        1 строка по умолчанию; растёт пропорционально длине detail,
        ограничено _MAX_ROW_HEIGHT чтобы один FAIL не съел весь экран
        (очень длинный detail в этом случае обрежется — лучше, чем
        одна строка съедает половину видимой таблицы).
        """
        if not detail:
            return 1
        needed = math.ceil(len(detail) / _DETAIL_COL_WIDTH)
        return max(1, min(needed, _MAX_ROW_HEIGHT))

    def _add_row(
        self, test: TestInfo, status: TestStatus, duration_ms: int, detail: str
    ) -> None:
        table = self.query_one("#results-table", DataTable)
        cells = self._row_cells(test, status, duration_ms, detail)
        table.add_row(*cells, key=test.id, height=self._row_height(detail))

    def _update_row(
        self, test_id: str, status: TestStatus, duration_ms: int, detail: str
    ) -> None:
        test = self._tests.get(test_id)
        if test is None:
            return
        self._statuses[test_id] = status

        table = self.query_one("#results-table", DataTable)
        cells = self._row_cells(test, status, duration_ms, detail)
        new_height = self._row_height(detail)

        # DataTable не предоставляет публичный API для изменения высоты
        # уже добавленной строки (update_cell меняет только содержимое).
        # Когда нужная высота отличается от текущей (например, тест перешёл
        # в FAIL с многострочным detail) — пересоздаём строку: remove + add.
        # Иначе — точечный update_cell, дешевле и не теряет курсор/scroll.
        current_height = table.get_row_height(test_id)
        if current_height != new_height:
            table.remove_row(test_id)
            table.add_row(*cells, key=test_id, height=new_height)
        else:
            col_keys = [c.key for c in table.ordered_columns]
            for col_key, value in zip(col_keys, cells):
                table.update_cell(test_id, col_key, value)

        self._resort(table)

    def _row_cells(
        self, test: TestInfo, status: TestStatus, duration_ms: int, detail: str
    ) -> tuple:
        """Собрать пять ячеек строки с учётом подсветки FAIL и переноса текста."""
        status_text, status_color = _STATUS_DISPLAY[status]
        is_fail = status == TestStatus.FAIL

        bg = _FAIL_BG if is_fail else None
        fg = "white" if is_fail else status_color

        name_style = Style(bgcolor=bg, bold=is_fail)
        hil_style = Style(bgcolor=bg, color="white" if is_fail else "cyan")
        status_style = Style(bgcolor=bg, color=fg, bold=True)
        time_style = Style(bgcolor=bg, color="white" if is_fail else "grey70")
        detail_style = Style(bgcolor=bg, color="white" if is_fail else "grey50")

        time_text = f"{duration_ms / 1000:.1f}с" if duration_ms > 0 else ""
        hil_text = "[*]" if test.requires_hil else "[-]"

        # overflow="fold" — перенос по символам на границе ячейки вместо
        # обрезания с многоточием (Textual default), detail виден целиком
        # на нескольких строках, если высота строки это позволяет.
        detail_cell = Text(detail, style=detail_style, overflow="fold")

        return (
            Text(test.name, style=name_style),
            Text(hil_text, style=hil_style),
            Text(status_text, style=status_style),
            Text(time_text, style=time_style),
            detail_cell,
        )

    def _resort(self, table: DataTable) -> None:
        """
        Пересортировать: FAIL наверх, дальше RUNNING/PENDING/SKIP/PASS.
        Внутри групп — исходный порядок реестра (стабильная сортировка).

        DataTable.sort(*columns, key=...) передаёт в key() кортеж значений
        ЯЧЕЕК (не row_key) для указанных columns — поэтому сортируем по
        содержимому самой ячейки "Тест" (используем как индекс в self._order)
        и по тексту статуса, который мы сами туда пишем и полностью
        контролируем — никаких приватных атрибутов DataTable не трогаем.
        """
        name_col = table.ordered_columns[0].key
        status_col = table.ordered_columns[2].key

        # text -> status enum, обратное к _STATUS_DISPLAY
        status_by_text = {text: status for status, (text, _) in _STATUS_DISPLAY.items()}
        # имя теста -> индекс в исходном реестре (для стабильности внутри группы)
        name_to_order = {
            self._tests[tid].name: idx
            for idx, tid in enumerate(self._order)
            if tid in self._tests
        }

        def sort_key(cells: tuple) -> tuple:
            name_cell, status_cell = cells
            name_plain = (
                name_cell.plain if hasattr(name_cell, "plain") else str(name_cell)
            )
            status_plain = (
                status_cell.plain if hasattr(status_cell, "plain") else str(status_cell)
            )
            status = status_by_text.get(status_plain, TestStatus.PENDING)
            order_idx = name_to_order.get(name_plain, 0)
            return (_SORT_RANK.get(status, 99), order_idx)

        table.sort(name_col, status_col, key=sort_key)
