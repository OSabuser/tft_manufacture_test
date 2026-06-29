"""screens — публичный экспорт экранов TUI."""

from .diag import DiagScreen
from .flash import FlashScreen
from .waiting import WaitingScreen

__all__ = ["WaitingScreen", "FlashScreen", "DiagScreen"]
