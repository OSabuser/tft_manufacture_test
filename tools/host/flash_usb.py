#!/usr/bin/env python3
"""
flash_usb.py — прошивка MIMXRT1052 через USB (BootROM SDP → Flashloader → blhost)

Использование:
    python3 flash_usb.py --firmware firmware_test --build-type Debug
    python3 flash_usb.py --firmware app           --build-type Release
    python3 flash_usb.py --firmware bootloader    --build-type Release
    python3 flash_usb.py --firmware firmware_test --build-type Debug --ram-only
    python3 flash_usb.py --bin-path /path/to/custom.bin
    python3 flash_usb.py --erase-chip

Шаги (прошивка):
    1. Устройство в SDP режиме (VID:PID 1FC9:0130)
       → sdphost загружает ivt_flashloader.bin в RAM
       → sdphost прыгает на flashloader
    2. Flashloader запущен (VID:PID 15A2:0073)
       → blhost конфигурирует FlexSPI NOR (пишет FCB)
       → blhost стирает нужный регион Flash
       → blhost пишет HAB образ начиная с 0x60001000
       → blhost reset

Шаги (chip erase):
    1. Загрузка Flashloader (аналогично прошивке)
    2. blhost configure-memory (инициализация FlexSPI контроллера)
    3. blhost flash-erase-all  (полная очистка W25Q, ~30 с)
    4. blhost reset

Конфигурация:
    Переменные окружения (задаются в .env, экспортируются через just):
        BOOTROM_VID       — VID BootROM SDP (default: 1fc9)
        BOOTROM_PID       — PID BootROM SDP (default: 0130)
        FLASHLOADER_VID   — VID Flashloader  (default: 15a2)
        FLASHLOADER_PID   — PID Flashloader  (default: 0073)
        BUILD_DIR         — путь к директории сборки (default: <repo_root>/build)
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

# ─── Пути ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR = Path(__file__).parent.resolve()
REPO_ROOT = SCRIPT_DIR.parent.parent
FLASHLOADER = SCRIPT_DIR / "dcd" / "ivt_flashloader.bin"

BUILD_DIR = Path(os.environ.get("BUILD_DIR", str(REPO_ROOT / "build")))

# ─── USB VID:PID ──────────────────────────────────────────────────────────────


def _usb(vid_key: str, vid_default: str, pid_key: str, pid_default: str) -> str:
    vid = os.environ.get(vid_key, vid_default).strip().upper().lstrip("0X")
    pid = os.environ.get(pid_key, pid_default).strip().upper().lstrip("0X")
    return f"0x{vid},0x{pid}"


SDP_USB = _usb("BOOTROM_VID", "1fc9", "BOOTROM_PID", "0130")
BLHOST_USB = _usb("FLASHLOADER_VID", "15a2", "FLASHLOADER_PID", "0073")

# ─── Аппаратные константы ─────────────────────────────────────────────────────
# FlexSPI NOR config option word: 0xC0000007
#   bits[31:28]=0xC — tag (QuadSPI NOR)
#   bits[3:0]=0x7   — option size
FLEXSPI_OPTION_ADDR = "0x2000"
FLEXSPI_OPTION_VALUE = "0xC0000007"
FLEXSPI_MEMORY_ID = "9"  # FlexSPI NOR memory interface ID

# Option word для записи FCB: tag=0xF → Write FCB command
FLEXSPI_FCB_VALUE = "0xF000000F"

# Flash layout
FLASH_BASE = 0x60000000
HAB_OFFSET = 0x1000  # IVT offset: write address = FLASH_BASE + HAB_OFFSET

ERASE_ALL_TIMEOUT_MS = "200000"  # W25Q512 стирается заметно дольше W25Q128

# ─── Helpers ──────────────────────────────────────────────────────────────────


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess:
    print(f"  $ {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=False)
    if check and result.returncode != 0:
        print(f"\n[ERROR] Команда завершилась с кодом {result.returncode}")
        sys.exit(1)
    return result


def step(msg: str) -> None:
    print(f"\n{'─' * 60}")
    print(f"  {msg}")
    print(f"{'─' * 60}")


def wait_for_flashloader(timeout: int = 10) -> bool:
    """Ждём пока Flashloader поднимется (опрашиваем blhost раз в секунду)."""
    print(f"\n  Ожидание Flashloader (до {timeout}с)...", end="", flush=True)
    for i in range(timeout):
        time.sleep(1)
        result = subprocess.run(
            ["blhost", "-u", BLHOST_USB, "-j", "--", "get-property", "1", "0"],
            capture_output=True,
        )
        if result.returncode == 0:
            print(f" OK ({i + 1}с)")
            return True
        print(".", end="", flush=True)
    print(" TIMEOUT")
    return False


def load_flashloader() -> None:
    """Загружает Flashloader через SDP если ещё не запущен."""
    result = subprocess.run(
        ["blhost", "-u", BLHOST_USB, "-j", "--", "get-property", "1", "0"],
        capture_output=True,
    )
    if result.returncode == 0:
        print("  Flashloader уже запущен — пропускаем загрузку")
        return

    if not FLASHLOADER.exists():
        print(f"[ERROR] Не найден: {FLASHLOADER}", file=sys.stderr)
        print(
            "  Скачай ivt_flashloader.bin из MCUXpresso Secure Provisioning Tool\n"
            "  и положи в tools/host/dcd/ivt_flashloader.bin",
            file=sys.stderr,
        )
        sys.exit(1)

    step("Загрузка Flashloader через SDP (1FC9:0130)")
    run(
        [
            "sdphost",
            "-u",
            SDP_USB,
            "-j",
            "--",
            "write-file",
            "0x20001C00",
            str(FLASHLOADER),
        ]
    )
    run(["sdphost", "-u", SDP_USB, "-j", "--", "jump-address", "0x20001C00"])

    if not wait_for_flashloader():
        print(
            "[ERROR] Flashloader не ответил. Проверь BOOT_MOD пины и подключение.",
            file=sys.stderr,
        )
        sys.exit(1)


def configure_flexspi() -> None:
    """Инициализирует FlexSPI NOR контроллер через Flashloader."""
    step("Конфигурация FlexSPI NOR (инициализация контроллера)")
    run(
        [
            "blhost",
            "-u",
            BLHOST_USB,
            "--",
            "fill-memory",
            FLEXSPI_OPTION_ADDR,
            "4",
            FLEXSPI_OPTION_VALUE,
            "word",
        ]
    )
    run(
        [
            "blhost",
            "-u",
            BLHOST_USB,
            "--",
            "configure-memory",
            FLEXSPI_MEMORY_ID,
            FLEXSPI_OPTION_ADDR,
        ]
    )


def write_fcb() -> None:
    """Записывает Flash Configuration Block в 0x60000000.

    Отдельный шаг после erase: Flashloader генерирует FCB из параметров FlexSPI
    и пишет его по адресу 0x60000000. Без FCB BootROM не знает как читать Flash.
    Option word 0xF000000F: tag=0xF → Write FCB command.
    """
    step("Запись FCB в Flash[0x60000000]")
    run(
        [
            "blhost",
            "-u",
            BLHOST_USB,
            "--",
            "fill-memory",
            FLEXSPI_OPTION_ADDR,
            "4",
            FLEXSPI_FCB_VALUE,
            "word",
        ]
    )
    run(
        [
            "blhost",
            "-u",
            BLHOST_USB,
            "--",
            "configure-memory",
            FLEXSPI_MEMORY_ID,
            FLEXSPI_OPTION_ADDR,
        ]
    )


def write_fcb_explicit(fcb_path: Path) -> None:
    """Записывает буквальный FCB-блоб (512 байт) в Flash[0x60000000].

    В отличие от write_fcb() (magic option word 0xF000000F — auto-config
    Flashloader, надёжно проверен только для W25Q128), здесь FCB пишется
    байт-в-байт через write-memory. Нужен для custom-бинарей: nxpimage
    всегда собирает "чистый" app-образ без FCB (см. hab_*.yaml — FCB туда
    не входит), поэтому его нужно подставлять явно под конкретный чип —
    tools/host/dcd/w25q128_fdcb.bin или tools/host/dcd/w25q512_fdcb.bin.
    """
    if not fcb_path.exists():
        print(f"[ERROR] FCB-файл не найден: {fcb_path}", file=sys.stderr)
        sys.exit(1)

    step(f"Запись явного FCB ({fcb_path.name}) в Flash[0x60000000]")
    run(
        [
            "blhost",
            "-u",
            BLHOST_USB,
            "--",
            "write-memory",
            f"0x{FLASH_BASE:08X}",
            str(fcb_path),
            "0",
        ]
    )


# ─── Основные операции ────────────────────────────────────────────────────────


def flash(hab_bin: Path, ram_only: bool = False, fcb_path: Path | None = None) -> None:
    """Прошить HAB-образ в Flash или загрузить в RAM."""
    if not hab_bin.exists():
        print(f"[ERROR] Файл не найден: {hab_bin}", file=sys.stderr)
        print(
            "  Сначала собери образ: uv run nxpimage hab export -c ...", file=sys.stderr
        )
        sys.exit(1)

    if ram_only:
        step(f"Загрузка в RAM (без записи во Flash): {hab_bin.name}")
        run(
            [
                "sdphost",
                "-u",
                SDP_USB,
                "-j",
                "--",
                "write-file",
                f"0x{FLASH_BASE + HAB_OFFSET:08X}",
                str(hab_bin),
            ]
        )
        run(
            [
                "sdphost",
                "-u",
                SDP_USB,
                "-j",
                "--",
                "jump-address",
                f"0x{FLASH_BASE + HAB_OFFSET:08X}",
            ]
        )
        return

    load_flashloader()
    configure_flexspi()

    write_addr = f"0x{FLASH_BASE + HAB_OFFSET:08X}"
    erase_size = ((HAB_OFFSET + hab_bin.stat().st_size + 0xFFF) // 0x1000) * 0x1000

    step(f"Прошивка Flash: {hab_bin.name}")
    print(f"  Образ:     {hab_bin}")
    print(f"  Размер:    {hab_bin.stat().st_size} байт")
    print(f"  Адрес:     {write_addr}")
    print(f"  Стирание:  0x{FLASH_BASE:08X} .. +{erase_size} байт")

    run(
        [
            "blhost",
            "-u",
            BLHOST_USB,
            "--",
            "flash-erase-region",
            f"0x{FLASH_BASE:08X}",
            str(erase_size),
            "0",
        ]
    )

    if fcb_path is not None:
        write_fcb_explicit(fcb_path)
    else:
        write_fcb()

    run(
        [
            "blhost",
            "-u",
            BLHOST_USB,
            "--",
            "write-memory",
            write_addr,
            str(hab_bin),
            "0",
        ]
    )

    step("Reset")
    run(["blhost", "-u", BLHOST_USB, "--", "reset"])
    print("\n  ✅  Прошивка завершена успешно")


def erase_chip() -> None:
    """Полная очистка Flash (chip erase) через Flashloader.

    Использует blhost flash-erase-all (memory ID 9 = FlexSPI NOR).
    Время операции: ~30 с для W25Q128.
    После erase FCB также стёрт — BootROM не сможет загрузить прошивку
    до следующей прошивки (flash_usb.py запишет FCB автоматически).
    """
    load_flashloader()
    configure_flexspi()

    step("Полная очистка Flash (chip erase, ~30 с)")
    print("  ⚠️   После chip erase BootROM не сможет загрузить прошивку.")
    print("  ⚠️   Используй flash_usb.py для восстановления.\n")
    run(
        [
            "blhost",
            "-t",
            ERASE_ALL_TIMEOUT_MS,
            "-u",
            BLHOST_USB,
            "--",
            "flash-erase-all",
            FLEXSPI_MEMORY_ID,
        ]
    )

    step("Reset")
    run(["blhost", "-u", BLHOST_USB, "--", "reset"])
    print("\n  ✅  Chip erase завершён")


# ─── CLI ──────────────────────────────────────────────────────────────────────


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Прошивка/очистка MIMXRT1052 через USB SDP",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    parser.add_argument(
        "--fcb-path",
        type=Path,
        metavar="PATH",
        default=None,
        help=(
            "Явный FCB-блоб (512 байт) для записи в 0x60000000 вместо "
            "auto-config Flashloader. Имеет смысл только с --bin-path."
        ),
    )

    target_group = parser.add_mutually_exclusive_group()
    target_group.add_argument(
        "--firmware",
        choices=["firmware_test", "bootloader", "app"],
        help="Стандартная прошивка из BUILD_DIR (требует --build-type)",
    )
    target_group.add_argument(
        "--bin-path",
        type=Path,
        metavar="PATH",
        help="Путь к произвольному HAB-бинарю (.bin) для прошивки",
    )
    target_group.add_argument(
        "--erase-chip",
        action="store_true",
        help="Полная очистка Flash (chip erase, ~30 с). Прошивка не выполняется.",
    )

    parser.add_argument(
        "--build-type",
        choices=["Debug", "Release"],
        default="Release",
        help="Тип сборки (только для --firmware, default: Release)",
    )
    parser.add_argument(
        "--ram-only",
        action="store_true",
        help="Загрузить в RAM без записи во Flash (только для --firmware / --bin-path)",
    )

    args = parser.parse_args()

    if args.erase_chip and args.ram_only:
        parser.error("--erase-chip несовместим с --ram-only")

    if args.fcb_path is not None and args.bin_path is None:
        parser.error("--fcb-path имеет смысл только вместе с --bin-path")

    if args.firmware is None and args.bin_path is None and not args.erase_chip:
        parser.error("Укажи --firmware, --bin-path или --erase-chip")

    # ── Определить hab_bin ──────────────────────────────────────────────────
    hab_bin: Path | None = None

    if args.firmware is not None:
        hab_bin = BUILD_DIR / args.build_type / f"{args.firmware}_hab.bin"
    elif args.bin_path is not None:
        hab_bin = args.bin_path.resolve()

    # ── Шапка ──────────────────────────────────────────────────────────────
    print(f"\n{'═' * 60}")
    print("  MIMXRT1052 Flash Tool (USB SDP)")
    if args.erase_chip:
        print("  Операция: chip erase")
    elif hab_bin is not None:
        label = (
            f"{args.firmware} [{args.build_type}]" if args.firmware else str(hab_bin)
        )
        print(f"  Прошивка:  {label}")
        print(f"  Образ:     {hab_bin}")
        if args.ram_only:
            print("  Режим:     RAM only (без записи во Flash)")
    print(f"  SDP USB:   {SDP_USB}")
    print(f"  BL  USB:   {BLHOST_USB}")
    print(f"{'═' * 60}\n")

    # ── Выполнить операцию ──────────────────────────────────────────────────
    if args.erase_chip:
        erase_chip()
    elif hab_bin is not None:
        flash(hab_bin, ram_only=args.ram_only, fcb_path=args.fcb_path)


if __name__ == "__main__":
    main()
