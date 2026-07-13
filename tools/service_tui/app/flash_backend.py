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

Р13 (полевой баг, ~50/500 плат — flash_erase_region/flash_erase_all
возвращали False с «устройство пропало с шины», хотя плата оставалась на
месте и стиралась штатно через NXP MCUBootUtility): два независимых бага.
(1) _fail_command() проверял присутствие BootROM SDP, а к моменту вызова
плата уже спрыгнула на Flashloader (см. load_flashloader()) — проверка
гарантированно возвращала «нет» независимо от реального состояния платы,
любой False от Flashloader-команды маскировался под обрыв USB. (2)
flash()/flash_erase_region и configure_flexspi() (в обоих путях) работали
на spsdk-дефолтном HID read-таймауте 2000мс (UsbDevice.__init__) — erase_chip()
поднимал таймаут только перед flash_erase_all, уже ПОСЛЕ configure_flexspi().
McuBoot(iface) создаётся с cmd_exception=False (дефолт) — таймаут не бросает
исключение, а тихо оседает в status_code=NO_RESPONSE, поэтому команда просто
возвращает False без трассировки. На части плат (полевые логи: ~2с до отказа
на flash_erase_region) реальный ответ не укладывался в 2с. Фикс: единый
MCUBOOT_CMD_TIMEOUT_MS выставляется сразу после load_flashloader(), до первой
команды сессии; _fail_command() проверяет detect_flashloader() и добавляет
mboot.status_string в текст ошибки — отличить в логе «host не дождался
ответа» (NoResponse) от настоящего кода ошибки устройства.

Р13, продолжение (после фикса выше status_string на живом железе показал
НАСТОЯЩУЮ причину — не таймаут, не обрыв связи): flash_erase_region/
flash_erase_all возвращают status 20106 «FlexSPINOR: Command Failure» —
дженерик-ошибка исполнения команды контроллером FlexSPI (НЕ 20101/20102
EraseSectorFail/EraseAllFail — до собственно попытки стереть дело не
доходит), причина пока НЕ найдена (см. ниже — одна из гипотез уже
опровергнута на железе).

Р13, ОПРОВЕРГНУТАЯ гипотеза (не повторять без новых данных): сверка с
boot_utility_log.txt (NXP MCUBootUtility, erase на той же плате проходит
штатно) показала порядок configure-memory(0xC0000007) [probe] →
configure-memory(0xF000000F) [commit FCB] → flash-erase-all — у нас
write_fcb_auto() (0xF000000F) либо не вызывался перед erase вообще
(erase_chip), либо вызывался только после (flash). Гипотеза была: commit
нужен ДО erase, чтобы контроллер полностью сконфигурировался под
erase-команды, а физически записанные им байты «всё равно сотрутся»
следующей erase-командой. **Это ПРОВЕРЕНО и ОПРОВЕРГНУТО на железе**:
добавление write_fcb_auto(mboot) сразу после configure_flexspi(mboot), до
erase, в обоих путях, дало НОВУЮ ошибку — status 10203 «Memory Cumulative
Write» (см. error_codes.py: соседний код IAP_CUMULATIVE_WRITE =
«Flash Memory Region To Be Programmed Is Not Empty») — И СЛОМАЛО ранее
рабочую плату. Значит configure-memory(0xF000000F) — это не «донастройка
контроллера», а НЕМЕДЛЕННАЯ физическая запись FCB во flash прямо в момент
вызова; запись в НЕ стёртую область (то есть в любую ранее прошитую плату)
эту запись сразу проваливает — она не успевает дождаться erase. Правка
отменена (revert). Почему в логе NXP это работало — неизвестно: либо их
0xF000000F-эквивалент в этой конкретной последовательности целится в уже
стёртую область по другой причине, либо это вообще не то, чем кажется на
первый взгляд (лог обрывается ровно на flash-erase-all, что было ДО или
ПОСЛЕ него в их полном сценарии — не зафиксировано). Не полагаться на
сопоставление логов двух разных инструментов без подтверждения по
официальной документации NXP (AN12107) или доступа к ROM-исходникам.

Р14 (переоценка после доп. вопросов пользователю): AN12107 не описывает
FCB/configure-memory вообще (проверено — pdftotext + grep по всему файлу,
0 совпадений). Форум NXP (RT1064) прямо говорит: configure-memory(0xC0000007)
«используется только для установления соединения» (речь идёт про скорость
FlexSPI), а не для настройки конкретного чипа — версия про «нужен pre-existing
корректный FCB на чипе» отклонена САМИМ пользователем: платы приходят с
завода с гарантированно пустым внешним flash, поэтому у «рабочих» 450 плат
просто не может быть валидного FCB к моменту первого касания. Уточняющие
вопросы дали решающий факт: на ~50 проблемных платах **обычная** прошивка
firmware_test (не только erase/«Другое») тоже падает, и чип/ревизия платы —
**та же**, что у 450 рабочих (не другая партия/поставщик). Значит проблема не
в логике конфигурации FlexSPI и не в конкретном чипе — она проявляется на
случайном подмножестве физически идентичных плат независимо от того, какая
именно flash-команда выполняется первой. Это типичная картина маргинального
электрического контакта (пайка/разводка/питание конкретного экземпляра),
воспроизводимого на erase (самая «тяжёлая» операция по току/длительности —
NOR erase требует внутренней подкачки напряжения), а не логической ошибки
хоста. Софт не может физически починить плохую пайку, но может дать шанс
операции пройти со второй/третьей попытки, если условие временное. Фикс:
retry (см. _run_flash_cmd) вокруг каждой команды, реально трогающей flash-чип
(configure_memory/erase/write) — если контакт временно «плавает», повторная
попытка через паузу может пройти там, где первая не удалась; если ошибка
детерминированная (чип действительно неисправен), retry её не замаскирует —
просто несколько раз повторит тот же честный отказ перед тем, как сдаться.
[Р15: retry не помог — все 3 попытки падали с тем же 20106. Гипотеза
«маргинальный контакт» опровергнута доп. фактами: MCUBootUtility на тех же
платах работает 10/10, а после ОДНОГО её касания плата НАВСЕГДА начинает
работать и в нашей утилите. Это не электрика — это энергонезависимое
состояние чипа. Retry оставлен как безвредная страховка.]

Р15 — НАЙДЕННАЯ ПРИЧИНА (misread лога в Р13): в логах MCUBootUtility
option word — десятичное 3221225991 = 0xC0000207, а НЕ 0xC0000007, как было
ошибочно прочитано при сверке в Р13 (и как стоило у нас с Фазы 0, унаследовано
от flash_usb.py). Разница — поле quad_mode_setting (биты [11:8]): 2 вместо 0.
quad_mode_setting=2 велит flashloader'у установить QE-бит (Quad Enable,
Status Register 2 bit 1 — формат Winbond) на самом чипе при configure-memory.
QE у Winbond ЭНЕРГОНЕЗАВИСИМЫЙ, и часть партий W25Q128 приходит с завода с
QE=0 (Winbond выпускает варианты с заводским QE=0 и QE=1 под почти одинаковой
маркировкой). Это объясняет ВСЁ наблюдённое детерминированно:
  - на чипах с QE=0 наш 0xC0000007 не включал quad-режим, LUT настроен на
    quad-команды → чип не отвечает → 20106 Command Failure на ЛЮБОЙ операции
    (erase/обычная прошивка/«Другое») — ровно как в поле (~50/500 плат);
  - MCUBootUtility (выбор W25Q в External Memory → 0xC0000207) работает
    10/10 и, единожды установив QE=1, НАВСЕГДА «чинит» плату и для нас;
  - на 450 «рабочих» платах — чипы из партий с заводским QE=1;
  - форумный лог RT1064 с 0xC0000007 не противоречит: MCUBootUtility
    подставляет option word по выбранному чипу, там чип другой.
Для чипов с уже установленным QE повторная установка — no-op (это статус-
регистр, не массив — Cumulative Write здесь не существует). Урок: десятичные
значения в чужих логах пересчитывать инструментом, не «на глаз» — misread
одного слова стоил трёх раундов на живом железе, включая один регресс.
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
# tools/service_tui/app/flash_backend.py → корень репозитория
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
# Р15: 0xC0000207, НЕ 0xC0000007. Поле quad_mode_setting (биты [11:8]) = 2 →
# flashloader при configure-memory устанавливает QE-бит (Quad Enable, Status
# Register 2 bit 1 — формат Winbond W25Q) на самом flash-чипе. QE у Winbond
# энергонезависимый; часть партий W25Q128 приходит с завода с QE=0 — на них
# quad_mode_setting=0 («QE не трогать») оставлял чип в SPI-режиме при
# LUT-таблицах, настроенных на quad-команды → ЛЮБАЯ операция (erase/write)
# падала с 20106 FlexSPINOR Command Failure. Значение снято с живого лога
# NXP MCUBootUtility (fill-memory ... 3221225991 = 0xC0000207 при выбранной
# памяти W25Q). Для чипов с уже установленным QE повторная установка — no-op.
FLEXSPI_OPTION_VALUE = 0xC0000207
FLEXSPI_FCB_VALUE = 0xF000000F  # tag=0xF → Write FCB command (auto-config)
FLEXSPI_MEMORY_ID = 9

# Единый read/write-таймаут HID-команд на всю Flashloader-сессию (configure +
# erase + write) — см. Р13. spsdk-дефолт 2000мс (UsbDevice.__init__) слишком
# короткий для части плат в поле; значение — эквивалент blhost -t 200000.
MCUBOOT_CMD_TIMEOUT_MS = 200_000
FLASHLOADER_WAIT_TIMEOUT_S = 10.0

# Р14/Р15 — повторные попытки flash-команды. Изначально добавлены под
# гипотезу «маргинальный контакт», которая позже опроверглась (настоящая
# причина — QE-бит, Р15). Оставлены как безвредная defensive-страховка от
# РЕАЛЬНО транзиентных сбоев USB/HID: happy-path не затрагивают (первая же
# успешная попытка возвращает управление), детерминированную ошибку не
# маскируют (после N попыток — тот же честный _fail_command со status_string).
# Компромисс: на действительно битой плате erase теперь может тянуться до
# N×~54с перед отказом — приемлемо, такие платы редки. См. модульный docstring.
CMD_RETRY_ATTEMPTS = 3
CMD_RETRY_DELAY_S = 0.5

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


def detect_flashloader() -> bool:
    """True если виден Flashloader (см. _FLASHLOADER_DEVICE_ID)."""
    return len(MbootUSBInterface.scan(device_id=_FLASHLOADER_DEVICE_ID)) > 0


def _flashloader_still_present() -> bool:
    """Быстрая проверка «плата ещё на шине» для error-путей (вариант B, Р10).

    Проверяет присутствие Flashloader, НЕ BootROM SDP (Р13, исторический
    баг): все вызовы _fail_command() приходят из команд McuBoot
    (configure_flexspi/write_fcb_*/flash_erase_*/write_memory), которые
    в принципе выполняются только после того, как плата спрыгнула с SDP на
    Flashloader внутри load_flashloader() — проверка по SDP тут возвращала
    бы «нет» ВСЕГДА, независимо от реального состояния платы.

    Любая ошибка самой проверки трактуется как «устройства нет»: проверка
    выполняется только ПОСЛЕ уже случившегося сбоя команды, шина в этот
    момент нестабильна, и «не смог проверить» практически всегда означает
    «плату выдернули» (согласовано, RELEASE_ROADMAP.md §B).
    """
    try:
        return detect_flashloader()
    except Exception:  # noqa: BLE001 — см. docstring: любой сбой ⇒ считаем обрывом
        return False


def _fail_command(mboot: McuBoot, message: str) -> None:
    """Живая команда spsdk вернула False — переклассификация по варианту B (Р10).

    Если устройство пропало с шины → обрыв (ConnectionLostError), иначе →
    честная ошибка операции (FlashBackendError). detect_flashloader()
    выполняется ТОЛЬКО здесь, в error-пути; happy path не затрагивается.
    mboot.status_string добавляется к тексту ошибки во всех случаях (Р13) —
    настоящий код статуса spsdk (например «NoResponse» при хостовом
    read-таймауте) отличим от кода ошибки, реально сообщённого устройством.

    :raises ConnectionLostError: устройство исчезло с шины после сбоя команды.
    :raises FlashBackendError: устройство на месте — ошибка самой операции.
    """
    full_message = f"{message} (status: {mboot.status_string})"
    if not _flashloader_still_present():
        raise ConnectionLostError(f"{full_message} (устройство пропало с шины)")
    raise FlashBackendError(full_message)


def _run_flash_cmd(mboot: McuBoot, description: str, cmd: Callable[[], bool]) -> None:
    """Выполняет одну flash-команду (configure_memory/erase/write) с повторами.

    Р14: часть плат в поле отдаёт `status FlexSPINOR Command Failure` на
    случайной операции с flash-чипом — похоже на маргинальный электрический
    контакт (пайка/питание конкретного экземпляра), а не на логическую
    ошибку хоста. Если условие временное, повтор через паузу может пройти
    там, где первая попытка не удалась; если ошибка детерминированная
    (плата действительно неисправна), повтор её не замаскирует — после
    исчерпания попыток ошибка всё равно уходит в _fail_command() с честным
    текстом. Между попытками проверяется присутствие Flashloader — при
    реальном обрыве связи retry не имеет смысла, сразу же уходим в
    _fail_command() (см. ConnectionLostError).

    :raises ConnectionLostError: устройство исчезло с шины между попытками.
    :raises FlashBackendError: команда не удалась во всех попытках,
        устройство на месте.
    """
    for attempt in range(1, CMD_RETRY_ATTEMPTS + 1):
        if cmd():
            return
        if attempt < CMD_RETRY_ATTEMPTS and _flashloader_still_present():
            logger.warning(
                f"{description}: попытка {attempt}/{CMD_RETRY_ATTEMPTS} не удалась "
                f"(status: {mboot.status_string}), повтор через {CMD_RETRY_DELAY_S}с"
            )
            time.sleep(CMD_RETRY_DELAY_S)
            continue
        _fail_command(mboot, f"{description} вернул False после {attempt} попыт(ок)")


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
    """Инициализирует FlexSPI NOR контроллер + включает QE-бит чипа (Р15).

    Option word 0xC0000207 (см. комментарий у константы) — flashloader
    пробует чип по SFDP и, что критично, устанавливает энергонезависимый
    QE-бит на чипах, пришедших с завода с QE=0. Второе option-слово
    обнуляется явно — зеркало последовательности NXP MCUBootUtility
    (при optionSize=0 оно не должно читаться, но страхуемся от мусора в RAM).

    ВАЖНО для «Другое»: flash() зовёт configure_flexspi() ВСЕГДА, в т.ч. для
    custom-бинарей с явным FCB. quad_mode_setting=2 = «QE через Status
    Register 2 bit 1, команда 0x31» — это метод Winbond. Все текущие
    FcbVariant — Winbond W25Q, для них корректно. Если в список памяти когда-
    нибудь добавят не-Winbond чип (другой метод QE — SR1 bit 6, отдельный
    регистр и т.п.), этот option word под него нужно будет пересмотреть.
    """
    mboot.fill_memory(FLEXSPI_OPTION_ADDR, 4, FLEXSPI_OPTION_VALUE)
    mboot.fill_memory(FLEXSPI_OPTION_ADDR + 4, 4, 0)
    _run_flash_cmd(
        mboot,
        "configure_memory (FlexSPI init)",
        lambda: mboot.configure_memory(FLEXSPI_OPTION_ADDR, FLEXSPI_MEMORY_ID),
    )


def write_fcb_auto(mboot: McuBoot) -> None:
    """Auto-config FCB через magic option word (см. flash_usb.py::write_fcb).

    Надёжно проверен только для W25Q128 — см. docstring оригинала.
    """
    mboot.fill_memory(FLEXSPI_OPTION_ADDR, 4, FLEXSPI_FCB_VALUE)
    _run_flash_cmd(
        mboot,
        "configure_memory (FCB write)",
        lambda: mboot.configure_memory(FLEXSPI_OPTION_ADDR, FLEXSPI_MEMORY_ID),
    )


def write_fcb_explicit(mboot: McuBoot, fcb_path: Path) -> None:
    """Пишет буквальный FCB-блоб (512 байт) в Flash[FLASH_BASE] (custom-бинари).

    См. flash_usb.py::write_fcb_explicit — nxpimage не кладёт FCB в HAB-образ,
    поэтому для произвольных чипов нужен явный блоб под конкретный memory chip.
    """
    if not fcb_path.exists():
        raise FlashBackendError(f"FCB-файл не найден: {fcb_path}")
    data = fcb_path.read_bytes()
    _run_flash_cmd(
        mboot,
        f"write_memory(FCB {fcb_path.name})",
        lambda: mboot.write_memory(FLASH_BASE, data, mem_id=0),
    )


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
    iface.device.timeout = MCUBOOT_CMD_TIMEOUT_MS  # см. Р13 — до первой команды

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
            _run_flash_cmd(
                mboot,
                "flash_erase_region",
                lambda: mboot.flash_erase_region(FLASH_BASE, erase_size, mem_id=0),
            )

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
            _run_flash_cmd(
                mboot,
                "write_memory (HAB-образ)",
                lambda: mboot.write_memory(
                    write_addr, data, mem_id=0, progress_callback=_on_progress
                ),
            )

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
    iface.device.timeout = MCUBOOT_CMD_TIMEOUT_MS  # см. Р13 — уже для configure_flexspi

    try:
        with McuBoot(iface) as mboot:
            _emit(progress_cb, "configure", 0, "Конфигурация FlexSPI NOR")
            configure_flexspi(mboot)

            _emit(progress_cb, "erase", 0, "Полная очистка Flash (~30с)")
            _run_flash_cmd(
                mboot,
                "flash_erase_all",
                lambda: mboot.flash_erase_all(mem_id=FLEXSPI_MEMORY_ID),
            )

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
