"""
flasher.py — async-обёртка над app/flash_backend.py для TUI.

Фаза 2: внутренности переведены с subprocess (flash_usb.py) на прямые
вызовы синхронного app/flash_backend.py через asyncio.to_thread. Публичный
API класса Flasher не менялся с Фазы 0 — сигнатуры те же, что в исходной
subprocess-версии.

Мост sync → async для прогресса: flash_backend отдаёт события через
синхронный callback (вызывается из worker-потока asyncio.to_thread), здесь
он оборачивается в asyncio.run_coroutine_threadsafe(...).result() —
согласовано с автором проекта (Фаза 2, вопрос 1): блокирующий .result()
внутри worker-потока гарантирует, что события прогресса приходят в TUI
строго по порядку.

Отмена прошивки посреди операции НЕ поддерживается и не должна
поддерживаться (согласовано, Фаза 2, вопрос 2) — если стирание/запись уже
начались, они обязаны докрутиться до конца (успешно или с ошибкой).
Специального кода для этого не потребовалось: Python-поток, запущенный
через asyncio.to_thread, нельзя прервать снаружи — если ожидающая
корутина в TUI получит CancelledError, сам поток всё равно продолжит
работу в фоне до естественного завершения flash_backend.flash()/
erase_chip(). Это и есть требуемое поведение, а не обходной путь.

Публичный API:
    Flasher.flash(target, bin_path, progress_cb) — async, прогресс через callback
    Flasher.erase_chip(progress_cb)              — async chip erase
    Flasher.detect_sdp()                         — проверить наличие BootROM SDP
    Flasher.detect_cdc()                         — проверить наличие CDC firmware_test
"""

from __future__ import annotations

import asyncio
import logging
import os
import shutil
import sys
from pathlib import Path
from typing import Awaitable, Callable, Optional

from . import flash_backend
from .models import FcbVariant, FlashProgress, FlashResult, FlashTarget

logger = logging.getLogger(__name__)

# Тип сборки firmware_test для прошивки (Debug | Release).
# Release временно нестабилен (см. отчёт о тестировании) — по умолчанию Debug.
_FIRMWARE_BUILD_TYPE = os.environ.get("FIRMWARE_BUILD_TYPE", "Debug")


def _firmware_hab_path(firmware: str) -> Path:
    """Делегирование в backend (Р6): dev/frozen резолв живёт там."""
    return flash_backend.firmware_hab_path(firmware, _FIRMWARE_BUILD_TYPE)


def _resolve_custom_binaries_dir() -> Path:
    """
    Директория с «сырыми» кастомными бинарниками для FlashScreen.

    Не пакуется в PyInstaller-бандл — внешняя директория, путь к которой
    можно переопределить через SERVICE_CUSTOM_BINARIES_DIR. sys.executable
    указывает на реальный exe и для --onefile, и для --onedir (в отличие
    от sys._MEIPASS — временной распаковки onefile).
    """
    override = os.environ.get("SERVICE_CUSTOM_BINARIES_DIR")
    if override:
        base = Path(override)
    elif getattr(sys, "frozen", False):
        base = Path(sys.executable).resolve().parent / "custom_binaries"
    else:
        base = Path(__file__).parents[1] / "custom_binaries"
    base.mkdir(parents=True, exist_ok=True)
    return base


CUSTOM_BINARIES_DIR = _resolve_custom_binaries_dir()

ProgressCallback = Callable[[FlashProgress], Awaitable[None]]


def _make_sync_progress_cb(
    progress_cb: Optional[ProgressCallback],
    loop: asyncio.AbstractEventLoop,
) -> Optional[flash_backend.ProgressCallback]:
    """Мост sync (flash_backend, вызывается из worker-потока) → async (TUI).

    .result() — намеренно блокирующий вызов внутри worker-потока: гарантирует
    доставку событий прогресса в порядке их возникновения (см. docstring
    модуля). Исключения из progress_cb (например, если экран уже закрыт)
    логируются и не прерывают саму операцию прошивки — см. договорённость
    по отмене (Фаза 2, вопрос 2): прошивка должна докрутиться до конца
    независимо от состояния UI.
    """
    if progress_cb is None:
        return None

    def _sync_cb(progress: FlashProgress) -> None:
        future = asyncio.run_coroutine_threadsafe(progress_cb(progress), loop)
        try:
            future.result()
        except Exception:
            logger.exception("progress_cb выбросил исключение из TUI-потока")

    return _sync_cb


def _format_error_message(exc: BaseException) -> str:
    """Человекочитаемое сообщение для FlashProgress(phase="error").

    Для ConnectionLostError (и любого FlashBackendError с connection_lost=True)
    добавляет явный префикс — вариант А (согласовано): специального перехода
    экрана нет, но в #flash-log причина должна читаться однозначно, без
    необходимости лезть в общий лог-файл за трейсбеком.
    """
    connection_lost = getattr(exc, "connection_lost", False)
    if connection_lost:
        return f"Соединение с платой потеряно: {exc}"
    return str(exc)


class Flasher:
    """
    Async-обёртка над app/flash_backend.py.

    Пример::

        flasher = Flasher()

        async def on_progress(p: FlashProgress) -> None:
            print(p.message)

        await flasher.flash(target=FlashTarget.FIRMWARE_TEST, progress_cb=on_progress)
    """

    # ── Detection (делегируется в flash_backend, см. Р7) ──────────────────

    @staticmethod
    def detect_sdp() -> bool:
        """True если виден BootROM SDP."""
        return flash_backend.detect_sdp()

    @staticmethod
    def detect_cdc() -> bool:
        """True если виден CDC firmware_test."""
        return flash_backend.detect_cdc()

    @staticmethod
    def list_custom_binaries() -> list[Path]:
        """Отсканировать custom_binaries/ на *.bin, отсортировано по имени."""
        return sorted(CUSTOM_BINARIES_DIR.glob("*.bin"))

    # ── Общий раннер: flash_backend-операция в потоке + перевод ошибок ────

    async def _run_flash_op(
        self,
        func: Callable[..., None],
        *args: object,
        async_progress_cb: Optional[ProgressCallback],
        sync_progress_cb: Optional[flash_backend.ProgressCallback],
        **kwargs: object,
    ) -> FlashResult:
        """Выполнить flash_backend.flash()/erase_chip() в потоке.

        flash_backend поднимает FlashBackendError (включая ConnectionLostError,
        см. Фазу 4) вместо возврата False — здесь это конвертируется обратно
        в контракт Flasher (FlashResult + событие phase="error"). connection_lost
        транслируется из exc.connection_lost (Фаза 4a, вариант 2) — экран
        различает физический обрыв от логической ошибки без парсинга текста.

        Отдельный except Exception — safety net (Фаза 4, согласовано):
        любое непредвиденное исключение из worker-потока (не только
        FlashBackendError) обязано вернуть управление в TUI с ok=False,
        а не оставить кнопки заблокированными навсегда. KeyboardInterrupt/
        SystemExit/CancelledError не перехватываются — это BaseException,
        не Exception, пробрасываются как есть
        """
        try:
            await asyncio.to_thread(func, *args, progress_cb=sync_progress_cb, **kwargs)
            return FlashResult(ok=True)
        except flash_backend.FlashBackendError as exc:
            logger.error("%s: %s", getattr(func, "__name__", func), exc)
            if async_progress_cb is not None:
                await async_progress_cb(
                    FlashProgress(
                        phase="error", percent=0, message=_format_error_message(exc)
                    )
                )
            return FlashResult(ok=False, connection_lost=exc.connection_lost)
        except Exception as exc:  # noqa: BLE001 — safety net, см. docstring
            logger.exception(
                "%s: непредвиденная ошибка", getattr(func, "__name__", func)
            )
            if async_progress_cb is not None:
                await async_progress_cb(
                    FlashProgress(
                        phase="error",
                        percent=0,
                        message=f"Непредвиденная ошибка: {exc}",
                    )
                )
            return FlashResult(ok=False, connection_lost=False)

    # ── Erase ────────────────────────────────────────────────────────────

    async def erase_chip(
        self,
        progress_cb: Optional[ProgressCallback] = None,
    ) -> FlashResult:
        """
        Chip erase Flash через USB SDP (flash_backend.erase_chip).
        Занимает ~30 с для W25Q128. FCB будет стёрт.

        :return: FlashResult(ok, connection_lost) — см. models.FlashResult.
        """
        loop = asyncio.get_running_loop()
        sync_cb = _make_sync_progress_cb(progress_cb, loop)
        return await self._run_flash_op(
            flash_backend.erase_chip,
            async_progress_cb=progress_cb,
            sync_progress_cb=sync_cb,
        )

    # ── Flash ────────────────────────────────────────────────────────────

    async def flash(
        self,
        target: FlashTarget,
        progress_cb: Optional[ProgressCallback] = None,
        bin_path: Optional[Path] = None,
        use_dcd: bool = False,
        fcb_variant: FcbVariant = FcbVariant.W25Q128,
    ) -> FlashResult:
        """
        Запустить прошивку через flash_backend.

        :param target:      Что прошиваем (firmware_test, production или custom).
        :param progress_cb: Async callback с FlashProgress (может быть None).
        :param bin_path:    Путь к бинарю (обязателен для CUSTOM; для
                             FIRMWARE_TEST — опционален, уже готовый HAB-образ
                             вместо штатной сборки из BUILD_DIR).
        :param use_dcd:     Только для CUSTOM — включить DCDFilePath (SDRAM-init)
                             при сборке HAB-образа через HabImage.
        :param fcb_variant: Только для CUSTOM — какой явный FCB-блоб (dcd/*_fdcb.bin)
                             записать в Flash[0x60000000] вместо auto-config.
        :return: FlashResult(ok, connection_lost) — см. models.FlashResult.
        """
        loop = asyncio.get_running_loop()
        sync_cb = _make_sync_progress_cb(progress_cb, loop)

        if target == FlashTarget.FIRMWARE_TEST:
            hab_bin = (
                bin_path.resolve()
                if bin_path is not None
                else _firmware_hab_path("firmware_test")
            )
            return await self._run_flash_op(
                flash_backend.flash,
                hab_bin,
                async_progress_cb=progress_cb,
                sync_progress_cb=sync_cb,
            )

        elif target == FlashTarget.PRODUCTION:
            result = await self._run_flash_op(
                flash_backend.flash,
                _firmware_hab_path("bootloader"),
                async_progress_cb=progress_cb,
                sync_progress_cb=sync_cb,
            )
            if result.ok:
                result = await self._run_flash_op(
                    flash_backend.flash,
                    _firmware_hab_path("app"),
                    async_progress_cb=progress_cb,
                    sync_progress_cb=sync_cb,
                )
            return result

        elif target == FlashTarget.CUSTOM:
            if bin_path is None:
                raise ValueError("FlashTarget.CUSTOM требует bin_path")
            return await self._flash_custom(
                bin_path, use_dcd, fcb_variant, progress_cb, sync_cb
            )

        return FlashResult(ok=False)

    async def _flash_custom(
        self,
        raw_bin_path: Path,
        use_dcd: bool,
        fcb_variant: FcbVariant,
        progress_cb: Optional[ProgressCallback],
        sync_cb: Optional[flash_backend.ProgressCallback],
    ) -> FlashResult:
        """
        Прошить «сырой» (не-HAB) кастомный бинарник из custom_binaries/.

        Два шага:
          1. Собрать HAB-образ (IVT + опционально DCD, БЕЗ FCB) через
             flash_backend.build_custom_hab() (HabImage, см. Фазу 0) — сам
             эмитит phase="hab_build" 0%/100% через sync_cb, отдельно
             дублировать здесь не нужно.
          2. Прошить получившийся HAB-образ через flash_backend.flash(),
             подставив явный FCB-блоб под выбранный тип памяти (FcbVariant).

        Временная директория build_custom_hab() (tempfile.mkdtemp) целиком
        удаляется в finally — не только *.bin, как было в subprocess-версии.
        """
        hab_bin = await self._build_custom_hab(
            raw_bin_path, use_dcd, progress_cb, sync_cb
        )
        if hab_bin is None:
            # Ошибка сборки HAB — чисто локальная операция (HabImage,
            # временный файл), к USB-соединению отношения не имеет.
            return FlashResult(ok=False)

        fcb_path = flash_backend.fcb_blob_path(fcb_variant.fcb_filename)
        try:
            return await self._run_flash_op(
                flash_backend.flash,
                hab_bin,
                fcb_path=fcb_path,
                async_progress_cb=progress_cb,
                sync_progress_cb=sync_cb,
            )
        finally:
            shutil.rmtree(hab_bin.parent, ignore_errors=True)

    async def _build_custom_hab(
        self,
        raw_bin_path: Path,
        use_dcd: bool,
        progress_cb: Optional[ProgressCallback],
        sync_cb: Optional[flash_backend.ProgressCallback],
    ) -> Optional[Path]:
        """Обёртка над flash_backend.build_custom_hab() в потоке.

        :return: путь к собранному *.hab.bin, либо None при ошибке.
        """
        try:
            return await asyncio.to_thread(
                flash_backend.build_custom_hab,
                raw_bin_path,
                use_dcd,
                progress_cb=sync_cb,
            )
        except flash_backend.FlashBackendError as exc:
            logger.error("build_custom_hab: %s", exc)
            if progress_cb is not None:
                await progress_cb(
                    FlashProgress(
                        phase="error",
                        percent=0,
                        message=f"Ошибка сборки HAB-образа: {_format_error_message(exc)}",
                    )
                )
            return None
        except Exception as exc:  # noqa: BLE001 — safety net, см. _run_flash_op
            logger.exception("build_custom_hab: непредвиденная ошибка")
            if progress_cb is not None:
                await progress_cb(
                    FlashProgress(
                        phase="error",
                        percent=0,
                        message=f"Непредвиденная ошибка сборки HAB-образа: {exc}",
                    )
                )
            return None
