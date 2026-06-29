#!/usr/bin/env python3
"""main.py — точка входа service-tui."""

from __future__ import annotations

import logging
import os
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
_ENV_FILE = _REPO_ROOT / ".env"

try:
    from dotenv import load_dotenv

    if _ENV_FILE.exists():
        load_dotenv(_ENV_FILE)
except ImportError:
    pass


def _setup_logging() -> None:
    log_dir = Path(os.environ.get("SERVICE_LOG_DIR", str(Path(__file__).parent)))
    log_file = log_dir / "service_tui.log"
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s  %(levelname)-8s  %(name)s  %(message)s",
        handlers=[logging.FileHandler(log_file, encoding="utf-8")],
    )
    logging.getLogger("textual").setLevel(logging.WARNING)


def main() -> None:
    _setup_logging()
    from app.app import ServiceApp

    ServiceApp().run()


if __name__ == "__main__":
    sys.exit(main() or 0)
