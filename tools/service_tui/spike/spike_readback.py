#!/usr/bin/env python3
"""
spike_readback.py — диагностика Гейта 3: читает Flash обратно и сравнивает
с ожидаемыми файлами (FCB-блоб @0x60000000, HAB-образ @0x60001000).

Запуск (плата в SDP или с уже поднятым Flashloader):
    uv run python spike/spike_readback.py \
        --expect-fcb ../host/dcd/w25q128_fdcb.bin \
        --expect-image /путь/к/собранному.hab.bin
Дампы кладёт рядом: readback_fcb.bin, readback_image.bin.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app import flash_backend as fb  # noqa: E402
from spsdk.mboot import McuBoot  # noqa: E402


def _cmp(label: str, actual: bytes, expected: bytes) -> bool:
    if actual == expected:
        print(f"  {label}: ✅ ПОБАЙТНО СОВПАДАЕТ ({len(actual)} байт)")
        return True
    n = min(len(actual), len(expected))
    diff_at = next((i for i in range(n) if actual[i] != expected[i]), n)
    print(
        f"  {label}: ❌ РАСХОЖДЕНИЕ с офсета 0x{diff_at:X} "
        f"(len actual={len(actual)}, expected={len(expected)})"
    )
    print(f"    actual  [{diff_at:#x}]: {actual[diff_at : diff_at + 16].hex()}")
    print(f"    expected[{diff_at:#x}]: {expected[diff_at : diff_at + 16].hex()}")
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--expect-fcb", type=Path, required=True)
    ap.add_argument("--expect-image", type=Path, required=True)
    args = ap.parse_args()

    exp_fcb = args.expect_fcb.read_bytes()
    exp_img = args.expect_image.read_bytes()

    iface = fb.load_flashloader(progress_cb=lambda p: print(f"  {p.message}"))
    ok = True
    with McuBoot(iface) as mboot:
        fb.configure_flexspi(mboot)

        fcb = mboot.read_memory(fb.FLASH_BASE, max(len(exp_fcb), 512), mem_id=0)
        img = mboot.read_memory(fb.FLASH_BASE + fb.HAB_OFFSET, len(exp_img), mem_id=0)
        if fcb is None or img is None:
            print("❌ read_memory вернул None")
            return 1

        Path("readback_fcb.bin").write_bytes(fcb)
        Path("readback_image.bin").write_bytes(img)

        ok = _cmp("FCB   @0x60000000", fcb[: len(exp_fcb)], exp_fcb) and ok
        ok = _cmp("Image @0x60001000", img, exp_img) and ok

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
