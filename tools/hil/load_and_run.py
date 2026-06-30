#!/usr/bin/env python3
"""
load_and_run.py — CLI-обёртка над pyocd_utils.load_and_run().

Использование (из директории tools/hil/):
    uv run python load_and_run.py firmware_test.elf
"""

import logging
import sys

from pyocd_utils import load_and_run

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)-8s] %(message)s",
    datefmt="%H:%M:%S",
)

if __name__ == "__main__":
    elf = sys.argv[1] if len(sys.argv) > 1 else "firmware_test.elf"
    load_and_run(elf)
    print("Done — смотри на LED!")
