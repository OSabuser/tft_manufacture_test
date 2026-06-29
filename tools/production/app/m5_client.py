"""
m5_client.py — клиент M5StampPLC для TUI.

Тонкая async-обёртка над синхронным JSON-lines протоколом M5 агента.
Blocking-вызовы выполняются в executor чтобы не блокировать Textual.

Поддерживаемые команды агента (JSON-lines):
    {"cmd": "ping"}
    {"cmd": "relay_set", "relay": 1..4, "state": true/false}
    {"cmd": "relay_get", "relay": 1..4}
    {"cmd": "can_send", "id": 0x100, "data": [0xDE, 0xAD, 0xBE, 0xEF]}
    {"cmd": "can_recv", "timeout_ms": 500}

Карта реле (из HIL_BENCH.md):
    RLY1 — питание таргета (VIN)
    RLY2 — RS_RX (оптовход RS)
    RLY3 — EXT_IN1 (оптовход IN1)
    RLY4 — EXT_IN2 (оптовход IN2)
"""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Optional

import serial
import serial.tools.list_ports

logger = logging.getLogger(__name__)

# VID/PID M5StampPLC (Espressif USB JTAG/serial)
_M5_VID = 0x303A
_M5_PID = 0x1001

_READLINE_TIMEOUT_S = 0.5
_CMD_TIMEOUT_S = 3.0


def _find_m5_port() -> Optional[str]:
    """Автодетект M5StampPLC по VID/PID."""
    for info in serial.tools.list_ports.comports():
        if info.vid == _M5_VID and info.pid == _M5_PID:
            return info.device
    return None


class M5Client:
    """
    Async-клиент для M5StampPLC.

    Пример::

        m5 = await M5Client.auto_connect()
        if m5:
            await m5.relay_set(3, True)   # RLY3 ON → EXT_IN1 ACTIVE
            await m5.relay_set(3, False)  # RLY3 OFF
            await m5.disconnect()
    """

    def __init__(self, port: str, baudrate: int = 115200) -> None:
        self._port = port
        self._baudrate = baudrate
        self._ser: Optional[serial.Serial] = None
        self._lock = asyncio.Lock()

    # ── Connection ──────────────────────────────────────────────────────────

    @classmethod
    async def auto_connect(cls, baudrate: int = 115200) -> Optional["M5Client"]:
        """
        Попытаться найти и подключиться к M5StampPLC.
        Вернуть None если не найден — M5 опционален.
        """
        port = _find_m5_port()
        if port is None:
            logger.info("M5StampPLC не найден")
            return None
        client = cls(port=port, baudrate=baudrate)
        try:
            await client.connect()
            return client
        except Exception as exc:
            logger.warning("M5StampPLC connect failed: %s", exc)
            return None

    async def connect(self) -> None:
        """Открыть порт и проверить связь через ping."""
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._open)
        ok = await self.ping()
        if not ok:
            await self.disconnect()
            raise ConnectionError(f"M5StampPLC не отвечает: {self._port}")

    def _open(self) -> None:
        self._ser = serial.Serial(
            port=self._port,
            baudrate=self._baudrate,
            timeout=_READLINE_TIMEOUT_S,
        )
        self._ser.reset_input_buffer()

    async def disconnect(self) -> None:
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._close)

    def _close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    @property
    def connected(self) -> bool:
        return self._ser is not None and self._ser.is_open

    # ── Low-level I/O ───────────────────────────────────────────────────────

    def _send_recv(self, cmd: dict) -> Optional[dict]:
        """Отправить команду, прочитать ответ (blocking)."""
        assert self._ser is not None
        line = json.dumps(cmd, separators=(",", ":")) + "\n"
        self._ser.write(line.encode("ascii"))
        self._ser.flush()
        raw = self._ser.readline()
        if not raw:
            return None
        try:
            return json.loads(raw.decode("ascii", errors="replace").strip())
        except json.JSONDecodeError:
            return None

    async def _cmd(self, cmd: dict) -> Optional[dict]:
        """Async wrapper над _send_recv."""
        loop = asyncio.get_running_loop()
        async with self._lock:
            return await loop.run_in_executor(None, self._send_recv, cmd)

    # ── Public API ──────────────────────────────────────────────────────────

    async def ping(self) -> bool:
        """Проверить связь с агентом."""
        resp = await self._cmd({"cmd": "ping"})
        return resp is not None and resp.get("status") == "ok"

    async def relay_set(self, relay: int, state: bool) -> bool:
        """
        Переключить реле.

        :param relay: Номер реле 1..4.
        :param state: True = ON, False = OFF.
        :return: True при успехе.
        """
        resp = await self._cmd({"cmd": "relay_set", "relay": relay, "state": state})
        return resp is not None and resp.get("status") == "ok"

    async def relay_get(self, relay: int) -> Optional[bool]:
        """
        Прочитать состояние реле.

        :return: True/False или None при ошибке.
        """
        resp = await self._cmd({"cmd": "relay_get", "relay": relay})
        if resp and resp.get("status") == "ok":
            return bool(resp.get("state"))
        return None

    async def can_send(self, can_id: int, data: list[int]) -> bool:
        """Отправить CAN-фрейм через M5."""
        resp = await self._cmd({"cmd": "can_send", "id": can_id, "data": data})
        return resp is not None and resp.get("status") == "ok"

    async def can_recv(self, timeout_ms: int = 500) -> Optional[dict]:
        """
        Принять CAN-фрейм.

        :return: {"id": int, "data": list[int]} или None при таймауте/ошибке.
        """
        resp = await self._cmd({"cmd": "can_recv", "timeout_ms": timeout_ms})
        if resp and resp.get("status") == "ok":
            return {"id": resp["id"], "data": resp["data"]}
        return None

    @staticmethod
    def find_port() -> Optional[str]:
        """Найти порт M5StampPLC. None если не найден."""
        return _find_m5_port()
