"""
env_config.py — конфигурация HIL из переменных окружения.

При запуске через just (set dotenv-load + set export) все переменные
из корневого .env автоматически попадают в os.environ до запуска pytest.

При прямом запуске pytest (без just) — выставить переменные вручную
или через `export HIL_VCOM_PORT=...` перед запуском.
"""

from __future__ import annotations
import os
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]


def _get(key: str, default: str) -> str:
    return os.environ.get(key, default)


BUILD_DIR: str = _get("HIL_BUILD_DIR", str(_REPO_ROOT / "build" / "target-debug"))
PYOCD_FREQUENCY: int = int(_get("HIL_PYOCD_FREQUENCY", "1000000"))


POWER_ON_SETTLE_S = float(_get("HIL_TARGET_POWER_SETTLE_S", "1.5"))

# VCOM на программаторе NXP MCU-Link
VCOM_PORT: str = _get("HIL_VCOM_PORT", "/dev/ttyACM0")
VCOM_BAUD: int = int(_get("HIL_VCOM_BAUD", "115200"))
READY_TIMEOUT: float = float(_get("HIL_READY_TIMEOUT", "5.0"))

# M5Stack StamPLC (промежуточная платформа для HIL)
M5_PORT: str = _get("HIL_M5_PORT", "/dev/ttyACM1")
M5_BAUD: int = int(_get("HIL_M5_BAUD", "115200"))
M5_TIMEOUT: float = float(_get("HIL_M5_TIMEOUT", "3.0"))

# VCOM на плате таргета
TARGET_VCOM_PORT: str = _get("HIL_USB_CDC_PORT", "/dev/ttyACM2")
TARGET_VCOM_BAUD: int = int(_get("HIL_USB_CDC_BAUD", "115200"))
TARGET_VCOM_TIMEOUT: float = float(_get("HIL_USB_CDC_TIMEOUT", "3.0"))
