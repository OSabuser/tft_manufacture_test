"""
firmware_client.py — async CDC-клиент firmware_test.

Транспорт: USB CDC ACM (pyserial в asyncio thread executor).
Протокол: JSON-lines v2 (один JSON-объект на строку, завершается '\\n').

Публичный API:
    FirmwareClient.connect()         — открыть порт, проверить ping→pong
    FirmwareClient.disconnect()      — закрыть порт
    FirmwareClient.ping()            — ping → pong, вернуть True/False
    FirmwareClient.list_tests()      — list_tests → list[TestInfo]
    FirmwareClient.run_selected()    — запустить тесты, вернуть AsyncGenerator событий
    FirmwareClient.send_confirm()    — отправить confirm
    FirmwareClient.get_uid()         — get_uid → str (hex UID)
    FirmwareClient.get_version()     — get_version → str (X.Y.Z)
    FirmwareClient.send_cmd_raw()    — отправить произвольную JSON-команду

Все blocking-операции с pyserial выполняются в run_in_executor()
чтобы не блокировать event loop Textual.
"""

from __future__ import annotations

import asyncio
import json
import logging
from typing import AsyncGenerator, Optional

import serial
import serial.tools.list_ports

from .models import TestInfo

logger = logging.getLogger(__name__)

_READLINE_TIMEOUT_S = 0.1
_PING_TIMEOUT_S = 5.0
# Длиннее самого долгого теста (SDRAM ~15 с)
_TEST_EVENT_TIMEOUT_S = 120.0


def _find_cdc_port(vid: int, pid: int) -> Optional[str]:
    """Найти первый CDC-порт с заданным VID/PID."""
    for info in serial.tools.list_ports.comports():
        if info.vid == vid and info.pid == pid:
            return info.device
    return None


def _parse_event(line: str) -> Optional[dict]:
    """Распарсить JSON-строку. Вернуть None при ошибке."""
    line = line.strip()
    if not line:
        return None
    try:
        return json.loads(line)
    except json.JSONDecodeError:
        logger.warning("Bad JSON from firmware: %r", line)
        return None


class FirmwareClient:
    """
    Async-клиент для общения с firmware_test по USB CDC ACM.

    Пример использования::

        client = FirmwareClient(port="/dev/ttyACM0", baudrate=115200)
        await client.connect()
        tests = await client.list_tests()
        async for event in client.run_selected([t.id for t in tests]):
            ...
        await client.disconnect()
    """

    def __init__(self, port: str, baudrate: int = 115200) -> None:
        self._port = port
        self._baudrate = baudrate
        self._ser: Optional[serial.Serial] = None
        self._lock = asyncio.Lock()

    # ── Connection ──────────────────────────────────────────────────────────

    async def connect(self) -> None:
        """Открыть порт и проверить связь через ping→pong."""
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._open)
        ok = await self.ping()
        if not ok:
            await self.disconnect()
            raise ConnectionError(f"firmware_test не отвечает на ping: {self._port}")

    def _open(self) -> None:
        self._ser = serial.Serial(
            port=self._port,
            baudrate=self._baudrate,
            timeout=_READLINE_TIMEOUT_S,
        )
        # сбросить входной буфер — session_start уже ушёл при старте прошивки
        self._ser.reset_input_buffer()

    async def disconnect(self) -> None:
        """Закрыть порт."""
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._close)

    def _close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    @classmethod
    async def auto_connect(
        cls, vid: int, pid: int, baudrate: int = 115200
    ) -> "FirmwareClient":
        """
        Найти CDC-порт по VID/PID и подключиться.

        :raises RuntimeError: если порт не найден.
        :raises ConnectionError: если ping не прошёл.
        """
        port = _find_cdc_port(vid, pid)
        if port is None:
            raise RuntimeError(f"CDC-порт с VID={vid:04X}:PID={pid:04X} не найден")
        client = cls(port=port, baudrate=baudrate)
        await client.connect()
        return client

    # ── Low-level I/O ───────────────────────────────────────────────────────

    def _write_line(self, obj: dict) -> None:
        """Сериализовать dict в JSON и отправить строку (blocking)."""
        assert self._ser is not None
        line = json.dumps(obj, separators=(",", ":")) + "\n"
        self._ser.write(line.encode("utf-8"))
        self._ser.flush()

    def _read_line(self) -> Optional[dict]:
        """Прочитать одну строку и распарсить (blocking, таймаут _READLINE_TIMEOUT_S)."""
        assert self._ser is not None
        raw = self._ser.readline()
        if not raw:
            return None
        return _parse_event(raw.decode("utf-8", errors="replace"))

    async def _send(self, obj: dict) -> None:
        """Отправить JSON-команду (async wrapper)."""
        loop = asyncio.get_running_loop()
        async with self._lock:
            await loop.run_in_executor(None, self._write_line, obj)

    async def _recv_until(
        self,
        stop_types: set[str],
        timeout_s: float = _TEST_EVENT_TIMEOUT_S,
    ) -> AsyncGenerator[dict, None]:
        """
        Читать события до получения одного из stop_types или таймаута.
        Генератор — yield каждого полученного события.

        При таймауте yield-ит синтетическое событие
        {"type": "_timeout", "timeout_s": ...} перед завершением — вызывающий
        код (Orchestrator) должен явно обработать этот тип и не путать его
        с обрывом связи без объяснения причины. Префикс "_" отличает это
        от реальных событий протокола firmware_test.
        """
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout_s
        while loop.time() < deadline:
            event = await loop.run_in_executor(None, self._read_line)
            if event is None:
                await asyncio.sleep(0)
                continue
            yield event
            if event.get("type") in stop_types:
                return
        logger.warning("_recv_until timeout after %.1f s", timeout_s)
        yield {"type": "_timeout", "timeout_s": timeout_s}

    # ── Public API ──────────────────────────────────────────────────────────

    async def ping(self) -> bool:
        """Отправить ping, ждать pong. Вернуть True при успехе."""
        await self._send({"type": "cmd", "cmd": "ping"})
        loop = asyncio.get_running_loop()
        deadline = loop.time() + _PING_TIMEOUT_S
        while loop.time() < deadline:
            event = await loop.run_in_executor(None, self._read_line)
            if event and event.get("type") == "pong":
                return True
            await asyncio.sleep(0)
        return False

    async def get_version(self) -> str:
        """Запросить версию firmware_test. Вернуть строку 'X.Y.Z' или ''."""
        await self._send({"type": "cmd", "cmd": "get_version"})
        async for event in self._recv_until({"version_response"}, timeout_s=3.0):
            if event.get("type") == "version_response":
                return event.get("fw", "")
        return ""

    async def list_tests(self) -> list[TestInfo]:
        """Запросить список тестов. Вернуть list[TestInfo]."""
        await self._send({"type": "cmd", "cmd": "list_tests"})
        async for event in self._recv_until({"test_list"}, timeout_s=5.0):
            if event.get("type") == "test_list":
                return [
                    TestInfo(
                        id=t["id"],
                        name=t["name"],
                        critical=t.get("critical", False),
                        requires_hil=t.get("requires_hil", False),
                    )
                    for t in event.get("tests", [])
                ]
        return []

    async def run_selected(self, test_ids: list[str]) -> AsyncGenerator[dict, None]:
        """
        Запустить выбранные тесты. Возвращает async generator событий:
        test_begin, test_result, confirm_request, summary, error.

        Вызывающий код должен обрабатывать confirm_request и вызывать
        send_confirm() не дожидаясь следующего события.
        """
        await self._send({"type": "cmd", "cmd": "run_selected", "tests": test_ids})
        async for event in self._recv_until(
            {"summary"}, timeout_s=_TEST_EVENT_TIMEOUT_S
        ):
            yield event

    async def send_confirm(self, confirm_id: str, confirmed: bool) -> None:
        """Отправить confirm в ответ на confirm_request."""
        await self._send({"type": "confirm", "id": confirm_id, "confirmed": confirmed})

    async def get_uid(self) -> str:
        """Запросить OCOTP UID. Вернуть hex-строку (16 символов) или ""."""
        await self._send({"type": "cmd", "cmd": "get_uid"})
        async for event in self._recv_until({"uid_response"}, timeout_s=3.0):
            if event.get("type") == "uid_response":
                return event.get("uid", "")
        return ""

    async def send_cmd_raw(self, cmd: dict) -> None:
        """Отправить произвольную команду (для отладки/расширения)."""
        await self._send(cmd)

    @staticmethod
    def find_port(vid: int, pid: int) -> Optional[str]:
        """Найти CDC-порт по VID/PID. None если не найден."""
        return _find_cdc_port(vid, pid)
