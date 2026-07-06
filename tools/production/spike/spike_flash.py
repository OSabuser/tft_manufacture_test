#!/usr/bin/env python3
"""
spike_flash.py — Фаза 0 (де-риск): SDP/Flashloader/McuBoot через spsdk
Python API вместо subprocess sdphost/blhost.

Закрывает:
  ⚠В2 — способ задать таймаут ≥200с для flash_erase_all (эквивалент
        `blhost -t 200000`): interface.device.timeout — МИЛЛИСЕКУНДЫ
        (проверено чтением spsdk/utils/interfaces/device/usb_device.py;
        докстрока UsbDevice.scan() говорит "секунды" — по факту мс,
        default=2000, что явно мс, а не секунды).
  Р7  — сигнатуры SdpUSBInterface.scan()/MbootUSBInterface.scan()
        (device_id="0xVVVV:0xPPPP", HID-транспорт через libusbsio,
        pyusb в детекте не участвует).
  О1  — вывод list_ports.comports() для CDC-устройств (firmware_test,
        M5StampPLC) с явными VID:PID — для ручной сверки на Windows.

Прогнать на Windows-машине БЕЗ Zadig — это и есть проверка Р7.

По умолчанию скрипт НЕ деструктивен: детект SDP → загрузка Flashloader →
get_property → configure_memory. Chip erase — только с --erase-all и
интерактивным подтверждением (как в flash_usb.py).

Запуск:
    uv run python spike_flash.py                # детект + get_property
    uv run python spike_flash.py --ports-only    # только serial-порты (О1)
    uv run python spike_flash.py --erase-all     # + chip erase (ДЕСТРУКТИВНО)
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path
from typing import Optional

from spsdk.mboot import McuBoot, MbootUSBInterface
from spsdk.mboot.properties import PropertyTag
from spsdk.sdp import SDP, SdpUSBInterface

# tools/production/spike/spike_flash.py → корень репозитория
REPO_ROOT = Path(__file__).resolve().parents[3]
FLASHLOADER_BIN = REPO_ROOT / "tools" / "host" / "dcd" / "ivt_flashloader.bin"

FLASHLOADER_LOAD_ADDR = 0x20001C00
FLEXSPI_OPTION_ADDR = 0x2000
FLEXSPI_OPTION_VALUE = 0xC0000007
FLEXSPI_MEMORY_ID = 9
ERASE_ALL_TIMEOUT_MS = 200_000  # эквивалент blhost -t 200000, см. docstring


def _device_id(vid_env: str, vid_default: str, pid_env: str, pid_default: str) -> str:
    """Тот же паттерн, что _usb() в tools/host/flash_usb.py, но в формате
    spsdk USBDeviceFilter: "0xVVVV:0xPPPP"."""
    vid = os.environ.get(vid_env, vid_default).strip().lower().removeprefix("0x")
    pid = os.environ.get(pid_env, pid_default).strip().lower().removeprefix("0x")
    return f"0x{vid}:0x{pid}"


SDP_ID = _device_id("BOOTROM_VID", "1fc9", "BOOTROM_PID", "0130")
FLASHLOADER_ID = _device_id("FLASHLOADER_VID", "15a2", "FLASHLOADER_PID", "0073")


def enumerate_serial_ports() -> None:
    """О1 + вопрос про Windows CDC: печатает VID:PID/описание всех serial-
    портов. firmware_test (1996:00AD) — class-compliant CDC ACM, драйвер
    не нужен ни на одной ОС (Windows 10+ грузит usbser.sys по классу
    интерфейса, не по VID:PID). M5StampPLC — открытый вопрос О1: нативный
    ESP32-S3 CDC (VID Espressif 303A, драйвер тоже не нужен) или мост
    CH9102/CP210x (на изолированной Windows потребует один вендорский
    драйвер — честная ОС-необходимость, не Zadig-костыль)."""
    import serial.tools.list_ports as list_ports

    print("=== Serial-порты (serial.tools.list_ports.comports) ===")
    ports = list(list_ports.comports())
    if not ports:
        print("  (ничего не найдено)")
        return
    for info in ports:
        vidpid = (
            f"{info.vid:04X}:{info.pid:04X}" if info.vid is not None else "----:----"
        )
        print(
            f"  {info.device:20s}  VID:PID={vidpid}  "
            f"{info.description!r}  serial={info.serial_number!r}"
        )
    print(
        "\n  Сверь: firmware_test ожидается как 1996:00AD. Для M5StampPLC "
        "запиши VID:PID из вывода выше — это и есть измерение О1 "
        "(см. MONOLITH_APP_PLAN.md §О1 и DEV_ARCH.md §5)."
    )


def wait_for_flashloader(timeout_s: float = 10.0) -> Optional[MbootUSBInterface]:
    print(f"  Ожидание Flashloader (до {timeout_s:.0f}с)...", end="", flush=True)
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = MbootUSBInterface.scan(device_id=FLASHLOADER_ID)
        if found:
            print(" OK")
            return found[0]
        time.sleep(0.5)
        print(".", end="", flush=True)
    print(" TIMEOUT")
    return None


def load_flashloader() -> Optional[MbootUSBInterface]:
    """SDP.write_file + jump_and_run — прямой аналог load_flashloader()
    из tools/host/flash_usb.py, но через spsdk API вместо sdphost."""
    already = MbootUSBInterface.scan(device_id=FLASHLOADER_ID)
    if already:
        print("  Flashloader уже запущен — пропускаем загрузку")
        return already[0]

    sdp_devices = SdpUSBInterface.scan(device_id=SDP_ID)
    if not sdp_devices:
        print(f"  SDP-устройство не найдено ({SDP_ID}). Плата в BootROM-режиме?")
        return None

    if not FLASHLOADER_BIN.exists():
        print(f"  Не найден: {FLASHLOADER_BIN}")
        return None

    print(f"  Загрузка Flashloader через SDP ({SDP_ID})")
    data = FLASHLOADER_BIN.read_bytes()
    with SDP(sdp_devices[0]) as sdp:
        sdp.write_file(FLASHLOADER_LOAD_ADDR, data)
        sdp.jump_and_run(FLASHLOADER_LOAD_ADDR)

    return wait_for_flashloader()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--ports-only",
        action="store_true",
        help="Только serial-порты (О1), без SDP/Flashloader",
    )
    parser.add_argument(
        "--erase-all", action="store_true", help="ДЕСТРУКТИВНО: chip erase после detect"
    )
    args = parser.parse_args()

    enumerate_serial_ports()
    if args.ports_only:
        return 0

    print(f"\n=== SDP/Flashloader (SDP={SDP_ID}, Flashloader={FLASHLOADER_ID}) ===")
    iface = load_flashloader()
    if iface is None:
        print("\n❌ Flashloader не поднялся — дальнейшие шаги пропущены")
        return 1

    with McuBoot(iface) as mboot:
        version = mboot.get_property(PropertyTag.CURRENT_VERSION)
        print(f"\n✅ get_property(CURRENT_VERSION) = {version}")

        print("\n=== configure_memory (FlexSPI NOR) ===")
        mboot.fill_memory(FLEXSPI_OPTION_ADDR, 4, FLEXSPI_OPTION_VALUE)
        ok = mboot.configure_memory(FLEXSPI_OPTION_ADDR, FLEXSPI_MEMORY_ID)
        print(f"configure_memory: {'OK' if ok else 'FAILED'}")

        if args.erase_all:
            print(
                "\n⚠️  Chip erase сотрёт FCB — плата не загрузится до повторной прошивки."
            )
            confirm = input("Наберите ERASE для подтверждения: ")
            if confirm != "ERASE":
                print("Отменено.")
                return 0
            print(
                f"  Выставляю timeout={ERASE_ALL_TIMEOUT_MS}мс (эквивалент blhost -t 200000)"
            )
            iface.device.timeout = ERASE_ALL_TIMEOUT_MS
            ok = mboot.flash_erase_all(mem_id=FLEXSPI_MEMORY_ID)
            print(f"flash_erase_all: {'OK' if ok else 'FAILED'}")
            mboot.reset(reopen=False)

    print("\n✅ Гейт 0 (SDP/Flashloader): пройден")
    return 0


if __name__ == "__main__":
    sys.exit(main())
