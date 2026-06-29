"""
models.py — типы данных TUI сервисного инженера.

Все модели — frozen dataclasses или IntEnum. Без бизнес-логики.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Optional


class AppMode(Enum):
    """Режим работы приложения — определяется автодетектом USB."""

    WAITING = auto()    # ждём подключения устройства
    FLASHING = auto()   # обнаружен BootROM SDP (1FC9:0130)
    DIAGNOSING = auto() # получен session_start по CDC


class TestStatus(Enum):
    """Статус выполнения теста."""

    PENDING = auto()
    RUNNING = auto()
    PASS = auto()
    FAIL = auto()
    SKIP = auto()


class FlashTarget(Enum):
    """Что прошиваем."""

    FIRMWARE_TEST = "firmware_test"
    PRODUCTION = "production"  # bootloader + tft_app
    CUSTOM = "custom"           # произвольный HAB-бинарь, путь задаётся отдельно


@dataclass(frozen=True)
class TestInfo:
    """Метаданные теста из list_tests."""

    id: str
    name: str
    critical: bool
    requires_hil: bool


@dataclass
class TestResult:
    """Результат выполнения теста."""

    id: str
    status: TestStatus
    duration_ms: int = 0
    detail: str = ""


@dataclass
class SessionState:
    """Состояние текущей диагностической сессии."""

    fw_version: str = ""
    target: str = ""
    chip_uid: str = ""
    m5_connected: bool = False
    tests: list[TestInfo] = field(default_factory=list)
    results: dict[str, TestResult] = field(default_factory=dict)

    def get_result(self, test_id: str) -> Optional[TestResult]:
        return self.results.get(test_id)

    def set_result(self, result: TestResult) -> None:
        self.results[result.id] = result


@dataclass(frozen=True)
class ConfirmRequest:
    """confirm_request от таргета."""

    id: str
    prompt: str
    timeout_ms: int


@dataclass(frozen=True)
class FlashProgress:
    """Прогресс прошивки."""

    phase: str       # "sdphost" | "blhost" | "done" | "error"
    percent: int     # 0..100
    message: str