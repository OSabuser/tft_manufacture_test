#!/usr/bin/env python3
"""
flash_usb.py — прошивка MIMXRT1052 через USB (BootROM SDP → Flashloader → blhost)

Использование:
    python3 flash_usb.py --firmware firmware_test --build-type Debug
    python3 flash_usb.py --firmware app           --build-type Release
    python3 flash_usb.py --firmware bootloader    --build-type Release
    python3 flash_usb.py --firmware firmware_test --build-type Debug --ram-only

Шаги:
    1. Устройство в SDP режиме (VID:PID 1FC9:0130)
       → sdphost загружает ivt_flashloader.bin в RAM
       → sdphost прыгает на flashloader
    2. Flashloader запущен (VID:PID 15A2:0073)
       → blhost конфигурирует FlexSPI NOR (пишет FCB)
       → blhost стирает нужный регион Flash
       → blhost пишет HAB образ начиная с 0x60002000
       → blhost reset
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

# ─── Константы ────────────────────────────────────────────────────────────────
SCRIPT_DIR    = Path(__file__).parent.resolve()
REPO_ROOT     = SCRIPT_DIR.parent.parent
BUILD_DIR     = REPO_ROOT / "build"
FLASHLOADER   = SCRIPT_DIR / "dcd" / "ivt_flashloader.bin"

# USB VID:PID
SDP_USB       = "0x1FC9,0x0130"   # BootROM Serial Download Protocol
BLHOST_USB    = "0x15A2,0x0073"   # Flashloader запущен

# FlexSPI NOR config option word: 0xC0000007
#   bits[31:28]=0xC — tag (QuadSPI NOR)
#   bits[3:0]=0x7   — option size
FLEXSPI_OPTION_ADDR  = "0x2000"
FLEXSPI_OPTION_VALUE = "0xC0000007"
FLEXSPI_MEMORY_ID    = "9"        # FlexSPI NOR memory interface ID

# Flash layout
FLASH_BASE    = 0x60000000
HAB_OFFSET    = 0x1000            # IVT offset: HAB binary starts with IVT at byte 0
                                   # write address = FLASH_BASE + ivtOffset = 0x60001000


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess:
    print(f"  $ {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=False)
    if check and result.returncode != 0:
        print(f"\n[ERROR] Команда завершилась с кодом {result.returncode}")
        sys.exit(1)
    return result


def step(msg: str) -> None:
    print(f"\n{'─'*60}")
    print(f"  {msg}")
    print(f"{'─'*60}")


def wait_for_flashloader(timeout: int = 10) -> bool:
    """Ждём пока Flashloader поднимется (VID:PID 15A2:0073)"""
    print(f"\n  Ожидание Flashloader (до {timeout}с)...", end="", flush=True)
    for i in range(timeout):
        time.sleep(1)
        result = subprocess.run(
            ["blhost", "-u", BLHOST_USB, "-j", "--", "get-property", "1", "0"],
            capture_output=True
        )
        if result.returncode == 0:
            print(f" OK ({i+1}с)")
            return True
        print(".", end="", flush=True)
    print(" TIMEOUT")
    return False


def load_flashloader() -> None:
    """Загружает Flashloader через SDP если ещё не запущен"""
    # Проверяем — вдруг уже запущен
    result = subprocess.run(
        ["blhost", "-u", BLHOST_USB, "-j", "--", "get-property", "1", "0"],
        capture_output=True
    )
    if result.returncode == 0:
        print("  Flashloader уже запущен — пропускаем загрузку")
        return

    if not FLASHLOADER.exists():
        print(f"[ERROR] Не найден: {FLASHLOADER}")
        print("  Скачай ivt_flashloader.bin из MCUXpresso Secure Provisioning Tool")
        print("  и положи в tools/host/dcd/ivt_flashloader.bin")
        sys.exit(1)

    step("Загрузка Flashloader через SDP (1FC9:0130)")
    run(["sdphost", "-u", SDP_USB, "-j", "--",
         "write-file", "0x20001C00", str(FLASHLOADER)])
    run(["sdphost", "-u", SDP_USB, "-j", "--",
         "jump-address", "0x20001C00"])

    if not wait_for_flashloader():
        print("[ERROR] Flashloader не ответил. Проверь BOOT_MOD пины и подключение.")
        sys.exit(1)


# Option word для записи FCB в Flash[0x60000000]
# 0xF000000F: tag=0xF → Write FCB command (Flashloader пишет готовый FCB в начало Flash)
FLEXSPI_FCB_VALUE    = "0xF000000F"


def configure_flexspi() -> None:
    """Инициализирует FlexSPI NOR контроллер через Flashloader"""
    step("Конфигурация FlexSPI NOR (инициализация контроллера)")
    run(["blhost", "-u", BLHOST_USB, "--",
         "fill-memory", FLEXSPI_OPTION_ADDR, "4", FLEXSPI_OPTION_VALUE, "word"])
    run(["blhost", "-u", BLHOST_USB, "--",
         "configure-memory", FLEXSPI_MEMORY_ID, FLEXSPI_OPTION_ADDR])


def write_fcb() -> None:
    """Записывает Flash Configuration Block в 0x60000000
    
    Отдельный шаг после erase! Flashloader генерирует FCB из параметров FlexSPI
    и пишет его по адресу 0x60000000. Без FCB BootROM не знает как читать Flash.
    Option word 0xF000000F: tag=0xF → Write FCB command.
    """
    step("Запись FCB в Flash[0x60000000]")
    run(["blhost", "-u", BLHOST_USB, "--",
         "fill-memory", FLEXSPI_OPTION_ADDR, "4", FLEXSPI_FCB_VALUE, "word"])
    run(["blhost", "-u", BLHOST_USB, "--",
         "configure-memory", FLEXSPI_MEMORY_ID, FLEXSPI_OPTION_ADDR])


def flash(hab_bin: Path, ram_only: bool = False) -> None:
    if not hab_bin.exists():
        print(f"[ERROR] Файл не найден: {hab_bin}")
        print("  Сначала собери образ: uv run nxpimage hab export -c ...")
        sys.exit(1)

    if ram_only:
        step(f"Загрузка в RAM (без записи во Flash): {hab_bin.name}")
        # RAM-only: грузим в RAM прямо через SDP без flashloader
        run(["sdphost", "-u", SDP_USB, "-j", "--",
             "write-file", f"0x{FLASH_BASE + HAB_OFFSET:08X}", str(hab_bin)])
        run(["sdphost", "-u", SDP_USB, "-j", "--",
             "jump-address", f"0x{FLASH_BASE + HAB_OFFSET:08X}"])
        return

    load_flashloader()
    configure_flexspi()      # 0xC0000007 — инициализация FlexSPI контроллера

    write_addr = f"0x{FLASH_BASE + HAB_OFFSET:08X}"
    # Размер стирания: HAB_OFFSET (0x1000) + размер образа, выровнено вверх до 4KB
    erase_size = ((HAB_OFFSET + hab_bin.stat().st_size + 0xFFF) // 0x1000) * 0x1000

    step(f"Прошивка Flash: {hab_bin.name}")
    print(f"  Образ:     {hab_bin}")
    print(f"  Размер:    {hab_bin.stat().st_size} байт")
    print(f"  Адрес:     {write_addr}")
    print(f"  Стирание:  0x{FLASH_BASE:08X} .. +{erase_size} байт")

    # memoryId=0 — mapped (XIP) адресное пространство
    run(["blhost", "-u", BLHOST_USB, "--",
         "flash-erase-region", f"0x{FLASH_BASE:08X}",
         str(erase_size), "0"])

    write_fcb()              # 0xF000000F — запись FCB в 0x60000000 (после erase!)

    # memoryId=0 — mapped (XIP) адресное пространство
    run(["blhost", "-u", BLHOST_USB, "--",
         "write-memory", write_addr, str(hab_bin), "0"])

    step("Reset")
    run(["blhost", "-u", BLHOST_USB, "--", "reset"])
    print("\n  ✓ Прошивка завершена успешно")


def main() -> None:
    parser = argparse.ArgumentParser(description="Прошивка MIMXRT1052 через USB")
    parser.add_argument("--firmware",   required=True,
                        choices=["firmware_test", "bootloader", "app"],
                        help="Имя прошивки")
    parser.add_argument("--build-type", required=True,
                        choices=["Debug", "Release"],
                        help="Тип сборки")
    parser.add_argument("--ram-only",   action="store_true",
                        help="Загрузить в RAM без записи во Flash")
    args = parser.parse_args()

    # HAB образ собирается командой: nxpimage hab export -c hab_<firmware>.yaml
    # Выходной файл: build/<BuildType>/<firmware>_hab.bin
    hab_bin = BUILD_DIR / args.build_type / f"{args.firmware}_hab.bin"

    print(f"\n{'═'*60}")
    print(f"  MIMXRT1052 Flash Tool")
    print(f"  Прошивка: {args.firmware}  [{args.build_type}]")
    print(f"  Образ:    {hab_bin}")
    print(f"{'═'*60}")

    flash(hab_bin, ram_only=args.ram_only)


if __name__ == "__main__":
    main()
