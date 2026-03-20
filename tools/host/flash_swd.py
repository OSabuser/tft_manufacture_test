#!/usr/bin/env python3
"""
flash_swd.py — прошивка MIMXRT1052 через SWD (MCU-Link / CMSIS-DAP)

Отличие от flash_usb.py (spsdk):
  flash_usb.py   — USB Serial Downloader Protocol (SDP). Плата должна быть в
                   режиме Serial Downloader (BOOT_MODE = 01). Использует ROM-
                   загрузчик для разбора IVT/DCD/HAB — сам конфигурирует FlexSPI
                   по DCD, поэтому FCB в образе не нужен.

  flash_swd.py   — SWD через отладочный пробник (MCU-Link). Плата в любом
                   режиме загрузки; отладчик загружает flash-алгоритм в RAM
                   и пишет NOR Flash напрямую. Boot ROM при cold-start читает
                   FCB первым — поэтому этот скрипт объединяет FCB + HAB
                   перед записью в единый образ.

Структура образа в Flash (XIP NOR):
  0x60000000  FCB  (512 байт) — Flash Configuration Block (W25Q128, Quad SPI)
  0x60000200  0xFF (padding)  — до адреса IVT
  0x60001000  IVT             — начало HAB-образа (ivtOffset = 0x1000)
  0x60001020  DCD             — инициализация SDRAM (если есть)
  0x60002000  .text / .data   — код прошивки

Конфигурация (приоритет: аргументы CLI > переменные окружения > defaults):
  PYOCD_TARGET     — таргет pyOCD          (default: mimxrt1050_quadspi)
  PYOCD_FREQUENCY  — частота SWD в Гц     (default: 4000000)
  BUILD_DIR        — директория сборки     (default: <repo_root>/build)
  FCB_PATH         — путь к FCB-бинарнику  (default: tools/host/dcd/w25q128_fdcb.bin)

Переменные задаются в .env и автоматически экспортируются через just (set export).

Использование:
  python flash_swd.py --firmware firmware_test --build-type Debug
  python flash_swd.py --firmware bootloader --build-type Release
  python flash_swd.py --firmware app --build-type Debug --dry-run
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# ── Корень репозитория ────────────────────────────────────────────────────────

REPO_ROOT = Path(__file__).resolve().parents[2]

# ── Конфигурация из окружения (just экспортирует .env через set export) ───────

def _env(key: str, default: str) -> str:
    return os.environ.get(key, default)

PYOCD_TARGET    = _env("PYOCD_TARGET",    "mimxrt1050_quadspi")
PYOCD_FREQUENCY = _env("PYOCD_FREQUENCY", "4000000")
BUILD_DIR       = Path(_env("BUILD_DIR",  str(REPO_ROOT / "build")))
FCB_PATH        = Path(_env("FCB_PATH",   str(REPO_ROOT / "tools/host/dcd/w25q128_fdcb.bin")))
if not FCB_PATH.is_absolute():
    FCB_PATH = REPO_ROOT / FCB_PATH
HIL_DIR         = REPO_ROOT / "tools" / "hil"

# IVT располагается по смещению 0x1000 от начала Flash (ivtOffset в HAB yaml)
IVT_OFFSET = 0x1000

# Маппинг имён прошивок → имена HAB-файлов (генерируются build::hab-*)
HAB_NAMES = {
    "firmware_test": "firmware_test_hab.bin",
    "bootloader":    "bootloader_hab.bin",
    "app":           "app_hab.bin",
}

# ── Helpers ───────────────────────────────────────────────────────────────────

def build_full_image(fcb_path: Path, hab_path: Path) -> bytes:
    """Объединить FCB + padding + HAB в единый образ для записи с 0x60000000."""
    fcb = fcb_path.read_bytes()
    hab = hab_path.read_bytes()

    if len(fcb) > IVT_OFFSET:
        raise ValueError(
            f"FCB size {len(fcb)} bytes exceeds IVT_OFFSET {IVT_OFFSET:#x}"
        )

    padding = b"\xff" * (IVT_OFFSET - len(fcb))  # 0xFF = erased flash value
    image   = fcb + padding + hab

    print(f"  FCB:     {len(fcb):>6} bytes  @ 0x60000000")
    print(f"  Padding: {len(padding):>6} bytes  @ 0x{0x60000000 + len(fcb):08X}")
    print(f"  HAB:     {len(hab):>6} bytes  @ 0x60001000")
    print(f"  Total:   {len(image):>6} bytes")

    return image


def run_pyocd_flash(image_path: Path, target: str, frequency: str) -> int:
    """Запустить pyocd flash через uv run из tools/hil."""
    cmd = [
        "uv", "run",
        "--directory", str(HIL_DIR),
        "pyocd", "flash",
        "--target",       target,
        "--frequency",    frequency,
        "--base-address", "0x60000000",
        "--erase",        "sector",
        str(image_path),
    ]
    print(f"\n  Running: {' '.join(cmd)}\n")
    return subprocess.run(cmd, cwd=REPO_ROOT).returncode


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Flash MIMXRT1052 via SWD (MCU-Link). "
                    "Prepends FCB to HAB image before programming.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--firmware",
        choices=list(HAB_NAMES.keys()),
        required=True,
        help="Firmware project to flash",
    )
    parser.add_argument(
        "--build-type",
        choices=["Debug", "Release"],
        default="Debug",
        help="CMake build type (default: Debug)",
    )
    parser.add_argument(
        "--fcb",
        type=Path,
        default=FCB_PATH,
        help=f"Path to FCB binary (default from FCB_PATH env or {FCB_PATH})",
    )
    parser.add_argument(
        "--target",
        default=PYOCD_TARGET,
        help=f"pyOCD target (default from PYOCD_TARGET env or {PYOCD_TARGET})",
    )
    parser.add_argument(
        "--frequency",
        default=PYOCD_FREQUENCY,
        help=f"SWD frequency in Hz (default from PYOCD_FREQUENCY env or {PYOCD_FREQUENCY})",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Build the combined image and save it, but do not flash",
    )
    args = parser.parse_args()

    # ── Проверить FCB ─────────────────────────────────────────────────────────
    if not args.fcb.exists():
        print(f"  ❌  FCB not found: {args.fcb}", file=sys.stderr)
        print(
            "  Hint: obtain w25q128_fdcb.bin from NXP SecureProvisioningTool\n"
            "        for W25Q128 in Quad SPI mode and place it in tools/host/dcd/\n"
            "        or set FCB_PATH in .env",
            file=sys.stderr,
        )
        return 1

    # ── Найти HAB-образ ───────────────────────────────────────────────────────
    hab_name = HAB_NAMES[args.firmware]
    hab_path = BUILD_DIR / args.build_type / hab_name

    if not hab_path.exists():
        print(f"  ❌  HAB image not found: {hab_path}", file=sys.stderr)
        fw_slug = args.firmware.replace("_", "-")
        bt_slug  = args.build_type.lower()
        print(
            f"  Hint: run  just build::hab-{fw_slug}-{bt_slug}  "
            "inside devcontainer first.",
            file=sys.stderr,
        )
        return 1

    print(f"\n  Firmware  : {args.firmware} ({args.build_type})")
    print(f"  HAB       : {hab_path}")
    print(f"  FCB       : {args.fcb}")
    print(f"  Target    : {args.target}")
    print(f"  Frequency : {args.frequency} Hz\n")

    # ── Собрать объединённый образ ────────────────────────────────────────────
    image = build_full_image(args.fcb, hab_path)

    if args.dry_run:
        out = BUILD_DIR / args.build_type / f"{args.firmware}_full.bin"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(image)
        print(f"\n  Dry run — image saved to {out}")
        return 0

    # ── Записать во Flash через pyOCD ─────────────────────────────────────────
    with tempfile.NamedTemporaryFile(
        suffix=f"_{args.firmware}_full.bin", delete=False
    ) as tmp:
        tmp_path = Path(tmp.name)
        tmp_path.write_bytes(image)

    try:
        rc = run_pyocd_flash(tmp_path, args.target, args.frequency)
    finally:
        tmp_path.unlink(missing_ok=True)

    if rc == 0:
        print(f"\n  ✅  {args.firmware} ({args.build_type}) flashed via SWD")
        print("  ⚡  Power cycle the board to boot from Flash")
    else:
        print(f"\n  ❌  pyocd flash failed (exit code {rc})", file=sys.stderr)

    return rc


if __name__ == "__main__":
    sys.exit(main())