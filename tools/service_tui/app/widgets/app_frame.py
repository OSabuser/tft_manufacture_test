"""
app_frame.py — общий контейнер фиксированного размера для всех экранов.

Решает две задачи:
1. UX: визуальная рамка фиксированного размера (как в ratatui-приложениях,
   напр. binsider), центрированная в терминале вне зависимости от размера окна.
2. Баг Textual 8.x: AssertionError при mouse drag, когда Screen выступает
   content_widget напрямую (assert isinstance(content_widget.parent, Widget)
   падает, потому что в этом сценарии родитель не является валидным Widget).
   AppFrame как промежуточный контейнер между Screen и содержимым устраняет
   этот сценарий — content_widget при drag теперь всегда AppFrame или его
   потомок, у которых .parent валиден.

Использование в экране::

    def compose(self) -> ComposeResult:
        with AppFrame():
            yield Static("...")
            yield Button("...")
"""

from __future__ import annotations

from textual.containers import Container


class AppFrame(Container):
    """Контейнер фиксированного размера, центрированный в Screen."""

    DEFAULT_CSS = """
    AppFrame {
        width: 100%;
        height: 100%;
        max-width: 160;
        max-height: 50;
        border: heavy $primary;
        background: $surface;
        padding: 1 2;
    }
    """
