"""
flash_backend.py — синхронное ядро прошивки MIMXRT1052 на spsdk Python API.

Прямой порт tools/host/flash_usb.py (subprocess sdphost/blhost) на прямые
вызовы spsdk (SDP/McuBoot/HabImage), провалидированные в Фазе 0
(spike_hab.py, spike_flash.py — байт-в-байт и на живом железе, macOS+Windows).

Zero Textual/asyncio импортов — модуль полностью синхронный и тестируемый
без event loop (см. test_flash_backend.py). Async-обвязка (asyncio.to_thread
+ run_coroutine_threadsafe) — забота Flasher (Фаза 2), не этого модуля.

Открытые вопросы/допущения (см. сопроводительное сообщение в чате):
  - Одна сессия McuBoot на весь flash()/erase_chip(), а не переоткрытие
    на каждую операцию, как в CLI flash_usb.py.
  - Имена FlashProgress.phase свои (не парсинг stdout blhost).
  - Иерархия исключений — предложение автора модуля, не зафиксирована
    отдельно в MONOLITH_APP_PLAN.md.
"""

from __future__ import annotations

import hashlib
import logging
import os
import tempfile
import time
from pathlib import Path
from typing import Callable, Optional

from spsdk.image.hab.hab_image import HabImage
from spsdk.mboot import McuBoot, MbootUSBInterface
from spsdk.sdp import SDP, SdpUSBInterface
from spsdk.utils.config import Config

from .models import FlashProgress
from .usb_ports import UsbId, resolve_serial_port

logger = logging.getLogger(__name__)

ProgressCallback = Callable[[FlashProgress], None]

# ─── Пути ────────────────────────────────────────────────────────────────
# tools/production/app/flash_backend.py → корень репозитория
REPO_ROOT = Path(__file__).resolve().parents[3]
_HOST_DCD_DIR = REPO_ROOT / "tools" / "host" / "dcd"
FLASHLOADER_BIN = _HOST_DCD_DIR / "ivt_flashloader.bin"
REAL_DCD_BIN = _HOST_DCD_DIR / "dcd.bin"


def fcb_blob_path(fcb_filename: str) -> Path:
    """Путь к готовому FCB-блобу (tools/host/dcd/w25q128_fdcb.bin и т.п.)."""
    return _HOST_DCD_DIR / fcb_filename


# ─── USB VID:PID (те же переменные окружения, что уже приняты в проекте) ──

_BOOTROM_VID = int(os.environ.get("BOOTROM_VID", "0x1fc9"), 16)
_BOOTROM_PID = int(os.environ.get("BOOTROM_PID", "0x0130"), 16)
_FLASHLOADER_VID = int(os.environ.get("FLASHLOADER_VID", "0x15a2"), 16)
_FLASHLOADER_PID = int(os.environ.get("FLASHLOADER_PID", "0x0073"), 16)
_CDC_VID = int(os.environ.get("SERVICE_CDC_VID", "0x1996"), 16)
_CDC_PID = int(os.environ.get("SERVICE_CDC_PID", "0x00ad"), 16)

_SDP_DEVICE_ID = f"0x{_BOOTROM_VID:04x}:0x{_BOOTROM_PID:04x}"
_FLASHLOADER_DEVICE_ID = f"0x{_FLASHLOADER_VID:04x}:0x{_FLASHLOADER_PID:04x}"

# ─── Аппаратные константы (см. flash_usb.py — значения не менялись) ──────

FLASH_BASE = 0x60000000
HAB_OFFSET = 0x1000
FLASHLOADER_LOAD_ADDR = 0x20001C00

FLEXSPI_OPTION_ADDR = 0x2000
FLEXSPI_OPTION_VALUE = 0xC0000007
FLEXSPI_FCB_VALUE = 0xF000000F  # tag=0xF → Write FCB command (auto-config)
FLEXSPI_MEMORY_ID = 9

ERASE_ALL_TIMEOUT_MS = 200_000  # эквивалент blhost -t 200000 (см. Фазу 0, ⚠В2)
FLASHLOADER_WAIT_TIMEOUT_S = 10.0

_HAB_OPTIONS_TEMPLATE = [
    "options:",
    "  flags: 0x00",
    "  startAddress: 0x60000000",
    "  ivtOffset: 0x1000",
    "  initialLoadSize: 0x2000",
    "  family: mimxrt1050",
]


# ─── Исключения ────────────────────────────────────────────────────────────


class FlashBackendError(Exception):
    """Базовая ошибка flash_backend."""


class DeviceNotFoundError(FlashBackendError):
    """SDP-устройство не найдено при попытке загрузить Flashloader."""


class FlashLoaderTimeoutError(FlashBackendError):
    """Flashloader не ответил за FLASHLOADER_WAIT_TIMEOUT_S после jump_and_run."""


class HabBuildError(FlashBackendError):
    """Ошибка сборки HAB-образа через HabImage (см. build_custom_hab)."""


# ─── Detection (Р7 — spsdk API вместо pyusb) ───────────────────────────────


def detect_sdp() -> bool:
    """True если виден BootROM SDP (см. _SDP_DEVICE_ID)."""
    return len(SdpUSBInterface.scan(device_id=_SDP_DEVICE_ID)) > 0


def detect_cdc() -> bool:
    """True если виден CDC firmware_test (список портов, см. usb_ports.py)."""
    return resolve_serial_port(UsbId(_CDC_VID, _CDC_PID)) is not None


def _emit(
    progress_cb: Optional[ProgressCallback], phase: str, percent: int, message: str
) -> None:
    if progress_cb is not None:
        progress_cb(FlashProgress(phase=phase, percent=percent, message=message))


# ─── Flashloader bring-up ───────────────────────────────────────────────────


def wait_for_flashloader(
    timeout_s: float = FLASHLOADER_WAIT_TIMEOUT_S,
) -> MbootUSBInterface:
    """Опрашивает MbootUSBInterface.scan() пока Flashloader не поднимется.

    :raises FlashLoaderTimeoutError: если не ответил за timeout_s.
    """
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = MbootUSBInterface.scan(device_id=_FLASHLOADER_DEVICE_ID)
        if found:
            return found[0]
        time.sleep(0.5)
    raise FlashLoaderTimeoutError(
        f"Flashloader не ответил за {timeout_s:.0f}с. Проверь BOOT_MOD пины и подключение."
    )


def load_flashloader(
    progress_cb: Optional[ProgressCallback] = None,
) -> MbootUSBInterface:
    """Загружает Flashloader через SDP, если ещё не запущен.

    :raises DeviceNotFoundError: SDP-устройство не найдено (и Flashloader тоже не поднят).
    :raises FlashLoaderTimeoutError: см. wait_for_flashloader().
    """
    already = MbootUSBInterface.scan(device_id=_FLASHLOADER_DEVICE_ID)
    if already:
        logger.info("Flashloader уже запущен — пропускаем загрузку")
        return already[0]

    sdp_devices = SdpUSBInterface.scan(device_id=_SDP_DEVICE_ID)
    if not sdp_devices:
        raise DeviceNotFoundError(
            f"SDP-устройство не найдено ({_SDP_DEVICE_ID}). Плата в BootROM-режиме?"
        )
    if not FLASHLOADER_BIN.exists():
        raise FlashBackendError(f"Не найден: {FLASHLOADER_BIN}")

    _emit(
        progress_cb,
        "load_flashloader",
        0,
        f"Загрузка Flashloader через SDP ({_SDP_DEVICE_ID})",
    )
    data = FLASHLOADER_BIN.read_bytes()
    with SDP(sdp_devices[0]) as sdp:
        sdp.write_file(FLASHLOADER_LOAD_ADDR, data)
        sdp.jump_and_run(FLASHLOADER_LOAD_ADDR)

    iface = wait_for_flashloader()
    _emit(progress_cb, "load_flashloader", 100, "Flashloader готов")
    return iface


# ─── FlexSPI / FCB ───────────────────────────────────────────────────────


def configure_flexspi(mboot: McuBoot) -> None:
    """Инициализирует FlexSPI NOR контроллер (см. flash_usb.py::configure_flexspi)."""
    mboot.fill_memory(FLEXSPI_OPTION_ADDR, 4, FLEXSPI_OPTION_VALUE)
    ok = mboot.configure_memory(FLEXSPI_OPTION_ADDR, FLEXSPI_MEMORY_ID)
    if not ok:
        raise FlashBackendError("configure_memory (FlexSPI init) вернул False")


def write_fcb_auto(mboot: McuBoot) -> None:
    """Auto-config FCB через magic option word (см. flash_usb.py::write_fcb).

    Надёжно проверен только для W25Q128 — см. docstring оригинала.
    """
    mboot.fill_memory(FLEXSPI_OPTION_ADDR, 4, FLEXSPI_FCB_VALUE)
    ok = mboot.configure_memory(FLEXSPI_OPTION_ADDR, FLEXSPI_MEMORY_ID)
    if not ok:
        raise FlashBackendError("configure_memory (FCB write) вернул False")


def write_fcb_explicit(mboot: McuBoot, fcb_path: Path) -> None:
    """Пишет буквальный FCB-блоб (512 байт) в Flash[FLASH_BASE] (custom-бинари).

    См. flash_usb.py::write_fcb_explicit — nxpimage не кладёт FCB в HAB-образ,
    поэтому для произвольных чипов нужен явный блоб под конкретный memory chip.
    """
    if not fcb_path.exists():
        raise FlashBackendError(f"FCB-файл не найден: {fcb_path}")
    data = fcb_path.read_bytes()
    ok = mboot.write_memory(FLASH_BASE, data, mem_id=0)
    if not ok:
        raise FlashBackendError(f"write_memory(FCB {fcb_path.name}) вернул False")


# ─── Прошивка / RAM-load / erase ────────────────────────────────────────────


def flash(
    hab_bin: Path,
    *,
    ram_only: bool = False,
    fcb_path: Optional[Path] = None,
    progress_cb: Optional[ProgressCallback] = None,
) -> None:
    """Прошить HAB-образ в Flash либо загрузить в RAM (см. flash_usb.py::flash).

    :param hab_bin: Готовый HAB-образ (.bin). Для custom-бинарей — см.
                     build_custom_hab().
    :param ram_only: Загрузить в RAM через SDP, во Flash не писать.
    :param fcb_path: Явный FCB-блоб (custom-бинари). None → auto-config
                      (write_fcb_auto, только для штатных firmware_test/production).
    :raises FlashBackendError: и подклассы — на любой ошибке.
    """
    if not hab_bin.exists():
        raise FlashBackendError(f"Файл не найден: {hab_bin}")

    if ram_only:
        _emit(progress_cb, "load_flashloader", 0, f"Загрузка в RAM: {hab_bin.name}")
        sdp_devices = SdpUSBInterface.scan(device_id=_SDP_DEVICE_ID)
        if not sdp_devices:
            raise DeviceNotFoundError(f"SDP-устройство не найдено ({_SDP_DEVICE_ID})")
        addr = FLASH_BASE + HAB_OFFSET
        with SDP(sdp_devices[0]) as sdp:
            sdp.write_file(addr, hab_bin.read_bytes())
            sdp.jump_and_run(addr)
        _emit(progress_cb, "done", 100, "Загружено в RAM")
        return

    iface = load_flashloader(progress_cb)

    write_addr = FLASH_BASE + HAB_OFFSET
    hab_size = hab_bin.stat().st_size
    erase_size = ((HAB_OFFSET + hab_size + 0xFFF) // 0x1000) * 0x1000

    with McuBoot(iface) as mboot:
        _emit(progress_cb, "configure", 0, "Конфигурация FlexSPI NOR")
        configure_flexspi(mboot)

        _emit(
            progress_cb, "erase", 0, f"Стирание 0x{FLASH_BASE:08X} + {erase_size} байт"
        )
        ok = mboot.flash_erase_region(FLASH_BASE, erase_size, mem_id=0)
        if not ok:
            raise FlashBackendError("flash_erase_region вернул False")

        _emit(progress_cb, "fcb", 0, "Запись FCB")
        if fcb_path is not None:
            write_fcb_explicit(mboot, fcb_path)
        else:
            write_fcb_auto(mboot)

        _emit(progress_cb, "write", 0, f"Запись {hab_bin.name} ({hab_size} байт)")

        def _on_progress(current: int, total: int) -> None:
            percent = int(current * 100 / total) if total else 0
            _emit(progress_cb, "write", percent, f"{current}/{total} байт")

        data = hab_bin.read_bytes()
        ok = mboot.write_memory(
            write_addr, data, mem_id=0, progress_callback=_on_progress
        )
        if not ok:
            raise FlashBackendError("write_memory (HAB-образ) вернул False")

        _emit(progress_cb, "reset", 0, "Reset")
        mboot.reset(reopen=False)

    _emit(progress_cb, "done", 100, "Прошивка завершена успешно")


def erase_chip(progress_cb: Optional[ProgressCallback] = None) -> None:
    """Полная очистка Flash (см. flash_usb.py::erase_chip). После erase FCB
    тоже стёрт — плата не загрузится до следующей прошивки."""
    iface = load_flashloader(progress_cb)

    with McuBoot(iface) as mboot:
        _emit(progress_cb, "configure", 0, "Конфигурация FlexSPI NOR")
        configure_flexspi(mboot)

        _emit(progress_cb, "erase", 0, "Полная очистка Flash (~30с)")
        iface.device.timeout = ERASE_ALL_TIMEOUT_MS  # см. Фазу 0, ⚠В2
        ok = mboot.flash_erase_all(mem_id=FLEXSPI_MEMORY_ID)
        if not ok:
            raise FlashBackendError("flash_erase_all вернул False")

        _emit(progress_cb, "reset", 0, "Reset")
        mboot.reset(reopen=False)

    _emit(progress_cb, "done", 100, "Chip erase завершён")


# ─── HAB-сборка для custom-бинарей (Фаза 0, ⚠В1 — HabImage вместо nxpimage) ─


def _make_hab_config(input_bin: Path, dcd_bin: Optional[Path], work_dir: Path) -> Path:
    """YAML-конфиг nxpimage hab с АБСОЛЮТНЫМИ путями (Р6 — временный файл
    может лежать где угодно, не обязательно рядом с реальными hab/*.yaml)."""
    lines = list(_HAB_OPTIONS_TEMPLATE)
    if dcd_bin is not None:
        lines.append(f"  DCDFilePath: {dcd_bin.resolve().as_posix()}")
    lines.append(f'inputImageFile: "{input_bin.resolve().as_posix()}"')
    lines.append("sections: []")

    yaml_path = (
        work_dir / f"hab_{hashlib.sha1(input_bin.name.encode()).hexdigest()[:8]}.yaml"
    )
    yaml_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return yaml_path


def build_custom_hab(
    raw_bin: Path,
    use_dcd: bool,
    progress_cb: Optional[ProgressCallback] = None,
) -> Path:
    """Собрать HAB-образ из сырого бинарника через HabImage (spsdk API).

    Заменяет flasher.py::_build_custom_hab (subprocess nxpimage). Логика
    сборки байт-в-байт идентична nxpimage CLI — см. Фазу 0 (golden-тест).

    :return: путь к собранному *.hab.bin во временной директории
             (tempfile.gettempdir(), Р6). Вызывающий код отвечает за unlink
             после использования — так же, как сейчас делает Flasher.
    :raises HabBuildError: при ошибке валидации/сборки.
    """
    _emit(progress_cb, "hab_build", 0, "Сборка HAB-образа (HabImage)")

    dcd_bin = REAL_DCD_BIN if use_dcd else None
    if use_dcd and not dcd_bin.exists():
        raise HabBuildError(f"DCD запрошен, но файл не найден: {dcd_bin}")

    work_dir = Path(tempfile.mkdtemp(prefix="tui_hab_"))
    try:
        yaml_path = _make_hab_config(raw_bin, dcd_bin, work_dir)
        cfg = Config.create_from_file(str(yaml_path))
        schemas = HabImage.get_validation_schemas_from_cfg(cfg)
        cfg.check(schemas, check_unknown_props=True)
        hab = HabImage.load_from_config(cfg)
        hab.post_export(cfg.config_dir)
        data = hab.export()
    except HabBuildError:
        raise
    except Exception as exc:  # noqa: BLE001 — оборачиваем весь spsdk-зоопарк исключений
        raise HabBuildError(f"Сборка HAB не удалась: {exc}") from exc

    out_path = work_dir / f"{raw_bin.stem}_hab.bin"
    out_path.write_bytes(data)
    _emit(progress_cb, "hab_build", 100, f"HAB-образ собран: {len(data)} байт")
    return out_path
