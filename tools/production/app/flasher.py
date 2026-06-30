"""
flasher.py — обёртка над tools/host/flash_usb.py для TUI.

Запускает flash_usb.py как subprocess, парсит stdout для прогресса,
пробрасывает события через asyncio.Queue в TUI.

Не дублирует spsdk-окружение — переиспользует tools/host/ uv-проект.

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
import re
from pathlib import Path
from typing import Awaitable, Callable, Optional

from .models import FlashProgress, FlashTarget

logger = logging.getLogger(__name__)

# VID/PID констант — читаются из env, fallback на известные значения
_BOOTROM_VID = int(os.environ.get("BOOTROM_VID", "0x1fc9"), 16)
_BOOTROM_PID = int(os.environ.get("BOOTROM_PID", "0x0130"), 16)
_CDC_VID = int(os.environ.get("SERVICE_CDC_VID", "0x1996"), 16)
_CDC_PID = int(os.environ.get("SERVICE_CDC_PID", "0x00ad"), 16)

# Тип сборки firmware_test для прошивки (Debug | Release).
# Release временно нестабилен (см. отчёт о тестировании) — по умолчанию Debug.
_FIRMWARE_BUILD_TYPE = os.environ.get("FIRMWARE_BUILD_TYPE", "Debug")

# Путь до flash_usb.py относительно корня репозитория
_FLASH_USB_SCRIPT = Path(__file__).parents[3] / "tools" / "host" / "flash_usb.py"
_HOST_TOOLS_DIR = _FLASH_USB_SCRIPT.parent

# Паттерны stdout flash_usb.py для извлечения прогресса
_RE_PERCENT = re.compile(r"(\d{1,3})\s*%")
_RE_PHASE = re.compile(r"(sdphost|blhost|Writing|Erasing|Verifying)", re.IGNORECASE)

ProgressCallback = Callable[[FlashProgress], Awaitable[None]]


def _detect_usb(vid: int, pid: int) -> bool:
    """
    Проверить наличие USB-устройства по VID/PID (синхронно).

    Два метода детекта:
    1. pyusb (usb.core) — видит все USB-устройства включая SDP bulk/HID.
       На macOS SDP-устройство (1FC9:0130) не создаёт serial-порт
       и невидимо через serial.tools.list_ports.
    2. serial.tools.list_ports — fallback для CDC ACM устройств.
    """
    # Метод 1: pyusb — работает для SDP и CDC
    try:
        import usb.core

        dev = usb.core.find(idVendor=vid, idProduct=pid)
        if dev is not None:
            return True
    except Exception:
        pass

    # Метод 2: serial list_ports — fallback для CDC ACM
    try:
        import serial.tools.list_ports

        for info in serial.tools.list_ports.comports():
            if info.vid == vid and info.pid == pid:
                return True
    except Exception:
        pass

    return False


class Flasher:
    """
    Async-обёртка над flash_usb.py.

    Пример::

        flasher = Flasher()
        await flasher.flash(
            target=FlashTarget.FIRMWARE_TEST,
            progress_cb=lambda p: print(p.message),
        )
    """

    def __init__(self) -> None:
        self._proc: Optional[asyncio.subprocess.Process] = None

    # ── Detection ───────────────────────────────────────────────────────────

    @staticmethod
    def detect_sdp() -> bool:
        """True если виден BootROM SDP (1FC9:0130)."""
        return _detect_usb(_BOOTROM_VID, _BOOTROM_PID)

    @staticmethod
    def detect_cdc() -> bool:
        """True если виден CDC firmware_test (1996:00AD)."""
        return _detect_usb(_CDC_VID, _CDC_PID)

    # ── Erase ────────────────────────────────────────────────────────────────

    async def erase_chip(
        self,
        progress_cb: Optional[ProgressCallback] = None,
    ) -> bool:
        """
        Chip erase Flash через USB SDP (flash_usb.py --erase-chip).
        Занимает ~30 с для W25Q128. FCB будет стёрт.

        :return: True при успехе.
        """
        cmd = [
            "uv",
            "run",
            "--directory",
            str(_HOST_TOOLS_DIR),
            "python",
            str(_FLASH_USB_SCRIPT),
            "--erase-chip",
        ]
        return await self._run_cmd(cmd, "erase", progress_cb)

    # ── Flash ────────────────────────────────────────────────────────────────

    async def flash(
        self,
        target: FlashTarget,
        progress_cb: Optional[ProgressCallback] = None,
        bin_path: Optional[Path] = None,
    ) -> bool:
        """
        Запустить прошивку через flash_usb.py.

        :param target:      Что прошиваем (firmware_test, production или custom).
        :param progress_cb: Async callback с FlashProgress (может быть None).
        :param bin_path:    Путь к бинарю (обязателен для CUSTOM).
        :return: True при успехе.
        """
        if target == FlashTarget.FIRMWARE_TEST:
            if bin_path is not None:
                return await self._run_flash_bin(bin_path, progress_cb)
            return await self._run_flash("firmware_test", progress_cb)
        elif target == FlashTarget.PRODUCTION:
            ok = await self._run_flash("bootloader", progress_cb)
            if ok:
                ok = await self._run_flash("app", progress_cb)
            return ok
        elif target == FlashTarget.CUSTOM:
            if bin_path is None:
                raise ValueError("FlashTarget.CUSTOM требует bin_path")
            return await self._run_flash_bin(bin_path, progress_cb)
        return False

    async def _run_flash(
        self,
        firmware: str,
        progress_cb: Optional[ProgressCallback],
    ) -> bool:
        """Запустить flash_usb.py для одного бинаря (стандартный firmware)."""
        cmd = [
            "uv",
            "run",
            "--directory",
            str(_HOST_TOOLS_DIR),
            "python",
            str(_FLASH_USB_SCRIPT),
            "--firmware",
            firmware,
            "--build-type",
            _FIRMWARE_BUILD_TYPE,
        ]
        return await self._run_cmd(cmd, firmware, progress_cb)

    async def _run_flash_bin(
        self,
        bin_path: Path,
        progress_cb: Optional[ProgressCallback],
    ) -> bool:
        """Запустить flash_usb.py для произвольного бинаря."""
        cmd = [
            "uv",
            "run",
            "--directory",
            str(_HOST_TOOLS_DIR),
            "python",
            str(_FLASH_USB_SCRIPT),
            "--bin-path",
            str(bin_path.resolve()),
        ]
        return await self._run_cmd(cmd, bin_path.stem, progress_cb)

    async def _run_cmd(
        self,
        cmd: list[str],
        label: str,
        progress_cb: Optional[ProgressCallback],
    ) -> bool:
        """Общий subprocess runner для всех операций flash_usb.py."""
        logger.info("Running: %s", " ".join(cmd))
        try:
            self._proc = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                cwd=str(_HOST_TOOLS_DIR),
            )
            assert self._proc.stdout is not None
            async for raw_line in self._proc.stdout:
                line = raw_line.decode("utf-8", errors="replace").rstrip()
                logger.debug("flash_usb [%s]: %s", label, line)
                if progress_cb is not None:
                    progress = _parse_progress(line)
                    if progress is not None:
                        await progress_cb(progress)
            await self._proc.wait()
            success = self._proc.returncode == 0
            if progress_cb is not None:
                phase = "done" if success else "error"
                msg = "Завершено" if success else "Ошибка"
                await progress_cb(FlashProgress(phase=phase, percent=100, message=msg))
            return success
        except Exception as exc:
            logger.error("Subprocess error [%s]: %s", label, exc)
            if progress_cb is not None:
                await progress_cb(
                    FlashProgress(phase="error", percent=0, message=str(exc))
                )
            return False
        finally:
            self._proc = None


def _parse_progress(line: str) -> Optional[FlashProgress]:
    """
    Извлечь прогресс из строки stdout flash_usb.py.
    Возвращает None если строка не несёт прогресс-информации.
    """
    percent_m = _RE_PERCENT.search(line)
    phase_m = _RE_PHASE.search(line)

    if percent_m is None and phase_m is None:
        return None

    percent = int(percent_m.group(1)) if percent_m else 0
    phase = phase_m.group(1).lower() if phase_m else "flash"
    return FlashProgress(phase=phase, percent=percent, message=line.strip())
