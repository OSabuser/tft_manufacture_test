"""
usb_ports.py — резолвер serial-портов по VID:PID (Р8).

Принцип (см. MONOLITH_APP_PLAN.md §Р8): имя порта не переносимо даже в
пределах одной ОС (перевтыкание в другой физический разъём меняет имя:
cu.usbmodemXXXX / COMn / ttyACMn), поэтому единственный стабильный
идентификатор устройства — VID:PID. Порядок резолва:

    1. Явный override через переменную окружения (escape hatch).
    2. Иначе — поиск по VID:PID среди serial.tools.list_ports.comports().
    3. Одно совпадение → его имя порта.
    4. Ноль совпадений → None (штатное состояние: устройство не подключено,
       для TUI это WaitingScreen, а не ошибка).
    5. Несколько совпадений → первое + warning в лог (дизамбигуация по
       serial_number — задел на будущее, см. MONOLITH_APP_PLAN.md §О3).

Модуль намеренно не хранит собственных VID/PID-констант — они остаются
там же, где были (firmware_client.py, m5_client.py, flash_backend.py),
каждый со своим fallback-дефолтом, как уже принято в проекте.
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from typing import Optional

import serial.tools.list_ports

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class UsbId:
    """VID:PID пара — единственный стабильный идентификатор USB-устройства."""

    vid: int
    pid: int

    def __str__(self) -> str:
        return f"{self.vid:04X}:{self.pid:04X}"


def resolve_serial_port(usb_id: UsbId, env_var: Optional[str] = None) -> Optional[str]:
    """
    Найти serial-порт устройства по VID:PID.

    :param usb_id: Идентичность устройства (VID:PID).
    :param env_var: Имя переменной окружения-override (escape hatch, п.1
                     алгоритма). Если задана и непуста — возвращается
                     as-is без обращения к list_ports; значение не
                     валидируется (ответственность вызывающего кода).
    :return: Имя порта (например, "/dev/cu.usbmodem1101" или "COM8"),
             либо None, если устройство не найдено.
    """
    if env_var:
        override = os.environ.get(env_var, "").strip()
        if override:
            logger.debug(
                "resolve_serial_port(%s): override из %s = %s",
                usb_id,
                env_var,
                override,
            )
            return override

    matches = [
        info.device
        for info in serial.tools.list_ports.comports()
        if info.vid == usb_id.vid and info.pid == usb_id.pid
    ]

    if not matches:
        return None

    if len(matches) > 1:
        logger.warning(
            "Несколько устройств %s: %s — используем первое (%s). ",
            usb_id,
            matches,
            matches[0],
        )

    return matches[0]
