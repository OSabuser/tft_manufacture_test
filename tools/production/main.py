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

    # Р9: во frozen _REPO_ROOT (.parents[2] от __file__) не указывает ни на
    # что осмысленное (синтетический путь внутри бандла) — раньше это молча
    # не находило файл. Явная проверка делает намерение видимым: frozen-
    # сборка работает без .env вообще (fallback-константы в коде), не
    # полагаясь на побочный эффект сломанного пути.
    if not getattr(sys, "frozen", False) and _ENV_FILE.exists():
        load_dotenv(_ENV_FILE)
except ImportError:
    pass

# Р12 (RELEASE_ROADMAP.md, Фаза 4b): эти логгеры на DEBUG печатают HID-байты
# КАЖДОЙ команды spsdk (TX-PACKET/RX-PACKET, сырые OUT[]/IN[] дампы) — именно
# они давали ~135 строк на одну прошивку (см. Гейт 4a, отчёт с железа).
# Подавляются до WARNING отдельно от root — иначе SERVICE_LOG_LEVEL=DEBUG для
# диагностики app.* терял бы смысл, утопленный в портянках чужого протокола.
_NOISY_LOGGERS = (
    "spsdk",
    "libusbsio",
    "libusbsio.hidapi.dev",
    "spsdk.mboot.protocol.bulk_protocol",
)


def _setup_logging() -> None:
    """Настроить логирование (Р12).

    Root по умолчанию — INFO (было DEBUG). Полный DEBUG, включая портянки
    spsdk/libusbsio, включается через SERVICE_LOG_LEVEL=DEBUG; при любом
    другом (или отсутствующем) значении _NOISY_LOGGERS принудительно
    приглушены до WARNING независимо от того, во что резолвится root.
    """
    default_log_dir = (
        Path(sys.executable).resolve().parent
        if getattr(sys, "frozen", False)
        else Path(__file__).parent
    )
    log_dir = Path(os.environ.get("SERVICE_LOG_DIR", str(default_log_dir)))
    log_file = log_dir / "service_tui.log"
    level_name = os.environ.get("SERVICE_LOG_LEVEL", "INFO").upper()
    root_level = getattr(logging, level_name, logging.INFO)
    logging.basicConfig(
        level=root_level,
        format="%(asctime)s  %(levelname)-8s  %(name)s  %(message)s",
        handlers=[logging.FileHandler(log_file, encoding="utf-8")],
    )
    logging.getLogger("textual").setLevel(logging.WARNING)

    if level_name != "DEBUG":
        for name in _NOISY_LOGGERS:
            logging.getLogger(name).setLevel(logging.WARNING)


def main() -> None:
    _setup_logging()
    from app.app import ServiceApp

    ServiceApp().run()


if __name__ == "__main__":
    sys.exit(main() or 0)
