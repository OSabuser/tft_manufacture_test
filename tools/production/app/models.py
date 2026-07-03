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

    WAITING = auto()  # ждём подключения устройства
    FLASHING = auto()  # обнаружен BootROM SDP (1FC9:0130)
    DIAGNOSING = auto()  # получен session_start по CDC


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
    CUSTOM = "custom"  # произвольный HAB-бинарь, путь задаётся отдельно


class FcbVariant(str, Enum):
    """Вариант FCB для кастомных бинарей.

    W25Q128/W25Q64 (3-байтная адресация) и W25Q256/W25Q512 (4-байтная)
    сведены к двум случаям — см. обсуждение прошивки старых плат.
    """

    W25Q128 = "w25q128"
    W25Q512 = "w25q512"

    @property
    def fcb_filename(self) -> str:
        """Имя файла в tools/host/dcd/, соответствующее варианту."""
        return f"{self.value}_fdcb.bin"

    @property
    def display_name(self) -> str:
        return {
            FcbVariant.W25Q128: "W25Q128 / W25Q64",
            FcbVariant.W25Q512: "W25Q256 / W25Q512",
        }[self]


@dataclass
class FlashPreset:
    """«Липкий» выбор оператора на FlashScreen.

    Живёт в памяти ServiceApp (не на диске), переносится на следующую
    плату в рамках одного запуска TUI — чтобы не выбирать заново файл
    и опции при прошивке партии одинаковых плат. Сбрасывается при
    перезапуске TUI. Обновляется в момент нажатия «Загрузить» (не только
    при успехе — неудача чаще всего про USB-кабель, а не про то, что
    выбор был неверным).

    DCD/FCB-поля имеют смысл только при target == FlashTarget.CUSTOM.
    """

    target: FlashTarget = FlashTarget.FIRMWARE_TEST
    custom_bin_name: Optional[str] = None
    use_dcd: bool = False
    fcb_variant: FcbVariant = FcbVariant.W25Q128


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

    phase: str  # "load_flashloader" | "configure" | "erase" | "fcb" | "write"
    # | "reset" | "hab_build" | "done" | "error"
    percent: int  # 0..100
    message: str
