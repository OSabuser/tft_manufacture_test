"""
flash_backend.py — синхронное ядро прошивки MIMXRT1052 на spsdk Python API.

Прямой порт tools/host/flash_usb.py (subprocess sdphost/blhost) на прямые
вызовы spsdk (SDP/McuBoot/HabImage), провалидированные в Фазе 0
(spike_hab.py, spike_flash.py — байт-в-байт и на живом железе, macOS+Windows).

Zero Textual/asyncio импортов — модуль полностью синхронный и тестируемый
без event loop (см. test_flash_backend.py). Async-обвязка (asyncio.to_thread
+ run_coroutine_threadsafe) — забота Flasher (Фаза 2), не этого модуля.

Открытые вопросы/допущения:
  - Одна сессия McuBoot на весь flash()/erase_chip(), а не переоткрытие
    на каждую операцию, как в CLI flash_usb.py.
  - Имена FlashProgress.phase свои (не парсинг stdout blhost).

Фаза 4: SPSDKConnectionError оборачивается в ConnectionLostError на всех
трёх точках отказа (SDP write, McuBoot handshake+команды, McuBoot chip erase),
что позволяет Flasher/TUI отличить обрыв USB от логической ошибки через
поле connection_lost. USB-интерфейс, полученный из load_flashloader(),
закрывается в finally на любом исходе (защита от утечки HID-хэндла в
редком окне «wait_for_flashloader вернул интерфейс → USB выдернут →
McuBoot.__enter__ упал»).
"""

from __future__ import annotations

import hashlib
import logging
import os
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable, Optional

from spsdk.exceptions import SPSDKConnectionError
from spsdk.image.hab.hab_image import HabImage
from spsdk.mboot import McuBoot, MbootUSBInterface
from spsdk.sdp import SDP, SdpUSBInterface
from spsdk.utils.config import Config
from spsdk.utils.exceptions import SPSDKTimeoutError

from .models import FlashProgress
from .usb_ports import UsbId, resolve_serial_port

logger = logging.getLogger(__name__)

ProgressCallback = Callable[[FlashProgress], None]

# ─── Пути ────────────────────────────────────────────────────────────────
# tools/production/app/flash_backend.py → корень репозитория
REPO_ROOT = Path(__file__).resolve().parents[3]


def _host_dcd_dir() -> Path:
    """Каталог с data-блобами (dcd.bin, ivt_flashloader.bin, *_fdcb.bin) —
    двухрежимный резолв (Р6), симметричный firmware_hab_path() (Фаза 5).

    Dev: tools/host/dcd/ (единый источник, использует и flash_usb.py).
    Frozen: sys._MEIPASS/data — см. service_tui.spec, который кладёт эти
    же файлы в 'data/' внутри бандла (для onedir _MEIPASS == _internal/).
    """
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / "data"
    return REPO_ROOT / "tools" / "host" / "dcd"


def flashloader_bin_path() -> Path:
    """Путь к ivt_flashloader.bin (двухрежимный резолв, см. _host_dcd_dir)."""
    return _host_dcd_dir() / "ivt_flashloader.bin"


def real_dcd_bin_path() -> Path:
    """Путь к dcd.bin (двухрежимный резолв, см. _host_dcd_dir)."""
    return _host_dcd_dir() / "dcd.bin"


def fcb_blob_path(fcb_filename: str) -> Path:
    """Путь к готовому FCB-блобу (tools/host/dcd/w25q128_fdcb.bin и т.п.)."""
    return _host_dcd_dir() / fcb_filename


def firmware_hab_path(firmware: str, build_type: str) -> Path:
    """Путь к готовому HAB-образу штатной прошивки (Р6, двухрежимный резолв)."""
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).resolve().parent / "firmware"
    else:
        base = Path(os.environ.get("BUILD_DIR", str(REPO_ROOT / "build")))
    return base / build_type / f"{firmware}_hab.bin"


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
    """Базовая ошибка flash_backend.

    :cvar connection_lost: True если ошибка связана с потерей физического
        соединения (USB выдернут, устройство пропало с шины). Позволяет
        Flasher/TUI отличить обрыв от логической ошибки без парсинга
        текста сообщения. По умолчанию False; подкласс ConnectionLostError
        переопределяет на True.
    """

    connection_lost: bool = False

    def __init__(self, message: str, *, connection_lost: bool = False) -> None:
        super().__init__(message)
        # instance-level override — на случай, если базовый класс поднимается
        # напрямую с connection_lost=True без использования ConnectionLostError
        if connection_lost:
            self.connection_lost = True


class ConnectionLostError(FlashBackendError):
    """Потеря USB-соединения посреди операции (обёртка над SPSDKConnectionError).

    Обёртка над spsdk.exceptions.SPSDKConnectionError, поднимается при
    исчезновении устройства с шины во время выполнения SDP/McuBoot команд.
    Отличается от DeviceNotFoundError, который возникает ДО начала операции
    (устройство никогда не было подключено).
    """

    connection_lost: bool = True

    def __init__(self, message: str) -> None:
        super().__init__(message, connection_lost=True)


class DeviceNotFoundError(FlashBackendError):
    """SDP-устройство не найдено при попытке загрузить Flashloader."""


class FlashLoaderTimeoutError(FlashBackendError):
    """Flashloader не ответил за FLASHLOADER_WAIT_TIMEOUT_S после jump_and_run."""


class HabBuildError(FlashBackendError):
    """Ошибка сборки HAB-образа через HabImage (см. build_custom_hab)."""


# SPSDKTimeoutError НЕ наследует SPSDKConnectionError (оба — потомки SPSDKError,
# проверено по исходникам spsdk 3.7.0), поэтому один `except SPSDKConnectionError`
# его пропускал → safety net в Flasher показывал «Непредвиденная ошибка» вместо
# «Соединение потеряно» (Фаза 4a). Ловим оба явным кортежем в SDP/McuBoot-обёртках:
# read-фаза после write может отдать голый таймаут. Именованная константа вместо
# 4× инлайн-дублей.
_CONNECTION_LOST_EXCEPTIONS = (SPSDKConnectionError, SPSDKTimeoutError)


# ─── Detection (Р7 — spsdk API вместо pyusb) ───────────────────────────────


def detect_sdp() -> bool:
    """True если виден BootROM SDP (см. _SDP_DEVICE_ID)."""
    return len(SdpUSBInterface.scan(device_id=_SDP_DEVICE_ID)) > 0


def detect_cdc() -> bool:
    """True если виден CDC firmware_test (список портов, см. usb_ports.py)."""
    return resolve_serial_port(UsbId(_CDC_VID, _CDC_PID)) is not None


def _sdp_still_present() -> bool:
    """Быстрая проверка «плата ещё на шине» для error-путей (вариант B, Р10).

    Любая ошибка самой проверки трактуется как «устройства нет»: проверка
    выполняется только ПОСЛЕ уже случившегося сбоя команды, шина в этот
    момент нестабильна, и «не смог проверить» практически всегда означает
    «плату выдернули» (согласовано, RELEASE_ROADMAP.md §B).
    """
    try:
        return detect_sdp()
    except Exception:  # noqa: BLE001 — см. docstring: любой сбой ⇒ считаем обрывом
        return False


def _fail_command(message: str) -> None:
    """Живая команда spsdk вернула False — переклассификация по варианту B (Р10).

    Если устройство пропало с шины → обрыв (ConnectionLostError), иначе →
    честная ошибка операции (FlashBackendError с прежним текстом). detect_sdp()
    выполняется ТОЛЬКО здесь, в error-пути; happy path не затрагивается.

    :raises ConnectionLostError: устройство исчезло с шины после сбоя команды.
    :raises FlashBackendError: устройство на месте — ошибка самой операции.
    """
    if not _sdp_still_present():
        raise ConnectionLostError(f"{message} (устройство пропало с шины)")
    raise FlashBackendError(message)


def _emit(
    progress_cb: Optional[ProgressCallback], phase: str, percent: int, message: str
) -> None:
    if progress_cb is not None:
        progress_cb(FlashProgress(phase=phase, percent=percent, message=message))


def _close_iface_quiet(iface: MbootUSBInterface) -> None:
    """Закрыть интерфейс в finally, глотая любые ошибки.

    Внутри McuBoot() как context-manager закрытие уже происходит, поэтому
    повторное close() на закрытом интерфейсе может выкинуть исключение
    из libusbsio — нам это не важно, мы просто хотим гарантию, что если
    McuBoot.__enter__ упал (окно между load_flashloader и with McuBoot()),
    интерфейс не остался висеть с открытым HID-хэндлом.
    """
    try:
        iface.close()
    except Exception:
        pass


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
    :raises ConnectionLostError: USB-соединение потеряно во время SDP-обмена.
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
    flashloader_bin = flashloader_bin_path()
    if not flashloader_bin.exists():
        raise FlashBackendError(f"Не найден: {flashloader_bin}")

    _emit(
        progress_cb,
        "load_flashloader",
        0,
        f"Загрузка Flashloader через SDP ({_SDP_DEVICE_ID})",
    )
    data = flashloader_bin.read_bytes()
    try:
        with SDP(sdp_devices[0]) as sdp:
            sdp.write_file(FLASHLOADER_LOAD_ADDR, data)
            sdp.jump_and_run(FLASHLOADER_LOAD_ADDR)
    except _CONNECTION_LOST_EXCEPTIONS as exc:
        raise ConnectionLostError(
            f"USB-соединение потеряно при загрузке Flashloader: {exc}"
        ) from exc

    iface = wait_for_flashloader()
    _emit(progress_cb, "load_flashloader", 100, "Flashloader готов")
    return iface


# ─── FlexSPI / FCB ───────────────────────────────────────────────────────


def configure_flexspi(mboot: McuBoot) -> None:
    """Инициализирует FlexSPI NOR контроллер (см. flash_usb.py::configure_flexspi)."""
    mboot.fill_memory(FLEXSPI_OPTION_ADDR, 4, FLEXSPI_OPTION_VALUE)
    ok = mboot.configure_memory(FLEXSPI_OPTION_ADDR, FLEXSPI_MEMORY_ID)
    if not ok:
        _fail_command("configure_memory (FlexSPI init) вернул False")


def write_fcb_auto(mboot: McuBoot) -> None:
    """Auto-config FCB через magic option word (см. flash_usb.py::write_fcb).

    Надёжно проверен только для W25Q128 — см. docstring оригинала.
    """
    mboot.fill_memory(FLEXSPI_OPTION_ADDR, 4, FLEXSPI_FCB_VALUE)
    ok = mboot.configure_memory(FLEXSPI_OPTION_ADDR, FLEXSPI_MEMORY_ID)
    if not ok:
        _fail_command("configure_memory (FCB write) вернул False")


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
        _fail_command(f"write_memory(FCB {fcb_path.name}) вернул False")


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
    :raises ConnectionLostError: обрыв USB посреди операции.
    """
    if not hab_bin.exists():
        raise FlashBackendError(f"Файл не найден: {hab_bin}")

    if ram_only:
        _emit(progress_cb, "load_flashloader", 0, f"Загрузка в RAM: {hab_bin.name}")
        sdp_devices = SdpUSBInterface.scan(device_id=_SDP_DEVICE_ID)
        if not sdp_devices:
            raise DeviceNotFoundError(f"SDP-устройство не найдено ({_SDP_DEVICE_ID})")
        addr = FLASH_BASE + HAB_OFFSET
        try:
            with SDP(sdp_devices[0]) as sdp:
                sdp.write_file(addr, hab_bin.read_bytes())
                sdp.jump_and_run(addr)
        except _CONNECTION_LOST_EXCEPTIONS as exc:
            raise ConnectionLostError(
                f"USB-соединение потеряно при RAM-загрузке: {exc}"
            ) from exc
        _emit(progress_cb, "done", 100, "Загружено в RAM")
        return

    iface = load_flashloader(progress_cb)

    write_addr = FLASH_BASE + HAB_OFFSET
    hab_size = hab_bin.stat().st_size
    erase_size = ((HAB_OFFSET + hab_size + 0xFFF) // 0x1000) * 0x1000

    # try/finally гарантирует закрытие iface даже если McuBoot.__enter__ падает
    # ДО того, как SDP context-manager отработает (тонкое окно, но реальное:
    # wait_for_flashloader вернул интерфейс, USB выдернут до McuBoot handshake).
    try:
        with McuBoot(iface) as mboot:
            _emit(progress_cb, "configure", 0, "Конфигурация FlexSPI NOR")
            configure_flexspi(mboot)

            _emit(
                progress_cb,
                "erase",
                0,
                f"Стирание 0x{FLASH_BASE:08X} + {erase_size} байт",
            )
            ok = mboot.flash_erase_region(FLASH_BASE, erase_size, mem_id=0)
            if not ok:
                _fail_command("flash_erase_region вернул False")

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
                _fail_command("write_memory (HAB-образ) вернул False")

            _emit(progress_cb, "reset", 0, "Reset")
            mboot.reset(reopen=False)
    except _CONNECTION_LOST_EXCEPTIONS as exc:
        raise ConnectionLostError(
            f"USB-соединение потеряно во время прошивки: {exc}"
        ) from exc
    finally:
        _close_iface_quiet(iface)

    _emit(progress_cb, "done", 100, "Прошивка завершена успешно")


def erase_chip(progress_cb: Optional[ProgressCallback] = None) -> None:
    """Полная очистка Flash (см. flash_usb.py::erase_chip). После erase FCB
    тоже стёрт — плата не загрузится до следующей прошивки.

    :raises ConnectionLostError: обрыв USB посреди chip erase.
    """
    iface = load_flashloader(progress_cb)

    try:
        with McuBoot(iface) as mboot:
            _emit(progress_cb, "configure", 0, "Конфигурация FlexSPI NOR")
            configure_flexspi(mboot)

            _emit(progress_cb, "erase", 0, "Полная очистка Flash (~30с)")
            iface.device.timeout = ERASE_ALL_TIMEOUT_MS  # см. Фазу 0, ⚠В2
            ok = mboot.flash_erase_all(mem_id=FLEXSPI_MEMORY_ID)
            if not ok:
                _fail_command("flash_erase_all вернул False")

            _emit(progress_cb, "reset", 0, "Reset")
            mboot.reset(reopen=False)
    except _CONNECTION_LOST_EXCEPTIONS as exc:
        raise ConnectionLostError(
            f"USB-соединение потеряно во время chip erase: {exc}"
        ) from exc
    finally:
        _close_iface_quiet(iface)

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

    dcd_bin = real_dcd_bin_path() if use_dcd else None
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
