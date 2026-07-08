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
import os
import asyncio
import json
import logging
import time
from typing import Optional

import serial
import serial.tools.list_ports

logger = logging.getLogger(__name__)

_M5_VID = int(os.environ.get("SERVICE_M5_VID", "0x303A"), 16)
_M5_PID = int(os.environ.get("SERVICE_M5_PID", "0x4001"), 16)

_READLINE_TIMEOUT_S = 0.5
_CMD_TIMEOUT_S = 3.0

# Реальная причина «M5 не виден с первого запуска» (подтверждено логами
# с живого стенда, см. историю m5_client.py/app.py) — НЕ в детекте порта:
# serial.tools.list_ports.comports() находит M5 мгновенно, с первой же
# попытки. Ломается connect() ПОСЛЕ открытия порта: первый ping не получает
# ответ за _READLINE_TIMEOUT_S. Похоже, само открытие serial-порта хостом
# перезапускает MicroPython на M5 (типично для USB-CDC ESP32-S3), а
# agent.py (tools/hil/m5/agent.py) перед основным циклом делает I2C/AW9523/
# CAN init и только потом пишет "READY" — на это уходит больше одного
# read-таймаута. На повторном запуске TUI (без переподключения M5) агент
# уже давно в основном цикле и отвечает мгновенно.
#
# Подтверждено логами: с бюджетом ~2.5с (6×0.5с) первый ping всё ещё не
# успевал (connect failed через 2.517с после найденного порта) — то есть
# перезагрузка/инициализация агента (I2C/AW9523/CAN) на живом стенде
# занимает заметно больше 2.5с. Бюджет увеличен с запасом; если снова не
# хватит — в логе будет видно сырое содержимое ответа (см. _send_recv),
# по нему можно будет откалибровать точнее вместо угадывания.
_AUTO_CONNECT_ATTEMPTS = 6
_AUTO_CONNECT_RETRY_DELAY_S = 0.5

_CONNECT_PING_ATTEMPTS = 24
_CONNECT_PING_RETRY_DELAY_S = 0.5


def _find_m5_port() -> Optional[str]:
    """Автодетект M5StampPLC по VID/PID."""
    for info in serial.tools.list_ports.comports():
        if info.vid == _M5_VID and info.pid == _M5_PID:
            return info.device
    return None


def _describe_visible_ports() -> str:
    """
    Дамп всех видимых serial-портов (device + VID:PID) для диагностики.

    Нужен, чтобы при неудаче auto_connect() в логе было видно, была ли на
    шине вообще хоть какая-то плата (и с каким VID:PID), а не просто
    «ничего не найдено» без возможности отличить «M5 не подключён» от
    «подключён, но не под тем VID:PID».
    """
    try:
        ports = list(serial.tools.list_ports.comports())
    except Exception as exc:
        return f"<comports() failed: {exc}>"
    if not ports:
        return "<нет портов>"
    return ", ".join(
        f"{info.device} ({info.vid:04X}:{info.pid:04X})"
        if info.vid is not None and info.pid is not None
        else f"{info.device} (vid/pid=None)"
        for info in ports
    )


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
        port = None
        loop = asyncio.get_running_loop()
        started = time.monotonic()
        for attempt in range(1, _AUTO_CONNECT_ATTEMPTS + 1):
            port = await loop.run_in_executor(None, _find_m5_port)
            if port is not None:
                elapsed = time.monotonic() - started
                logger.info(
                    "M5StampPLC найден с попытки %d/%d (%.1fс, ищем %04X:%04X): %s",
                    attempt,
                    _AUTO_CONNECT_ATTEMPTS,
                    elapsed,
                    _M5_VID,
                    _M5_PID,
                    port,
                )
                break
            if attempt < _AUTO_CONNECT_ATTEMPTS:
                await asyncio.sleep(_AUTO_CONNECT_RETRY_DELAY_S)
        if port is None:
            elapsed = time.monotonic() - started
            visible = await loop.run_in_executor(None, _describe_visible_ports)
            logger.info(
                "M5StampPLC не найден за %.1fс (%d попыток, ищем %04X:%04X). "
                "Видимые порты: %s",
                elapsed,
                _AUTO_CONNECT_ATTEMPTS,
                _M5_VID,
                _M5_PID,
                visible,
            )
            return None
        client = cls(port=port, baudrate=baudrate)
        try:
            await client.connect()
            return client
        except Exception as exc:
            logger.warning("M5StampPLC connect failed: %s", exc)
            return None

    async def connect(self) -> None:
        """
        Открыть порт и проверить связь через ping.

        Первый ping сразу после открытия порта нередко не долетает: судя по
        логам (см. m5_client.py история), порт находится и открывается
        мгновенно, но agent.py (tools/hil/m5/agent.py) перед тем как дойти
        до основного цикла и ответить на первую команду, делает I2C/AW9523/
        CAN init и пишет "READY" — вероятно, само открытие serial-порта
        хостом перезапускает MicroPython (типично для USB-CDC ESP32-S3), и
        эта инициализация занимает больше одного read-таймаута. mpremote
        (just host::m5-*) с этим не сталкивается — либо вообще не открывает
        порт (m5-scan — чистое перечисление), либо переживает reset за счёт
        своей протокольной логики поверх REPL. Здесь — короткий retry ping
        вместо одной попытки с жёстким таймаутом.
        """
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._open)
        for attempt in range(1, _CONNECT_PING_ATTEMPTS + 1):
            if await self.ping():
                return
            if attempt < _CONNECT_PING_ATTEMPTS:
                await asyncio.sleep(_CONNECT_PING_RETRY_DELAY_S)
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
        self._ser.write(line.encode("utf-8"))
        self._ser.flush()
        raw = self._ser.readline()
        if not raw:
            logger.debug(
                "_send_recv(%s): нет ответа за %.1fс (timeout)",
                cmd.get("cmd"),
                _READLINE_TIMEOUT_S,
            )
            return None
        try:
            return json.loads(raw.decode("utf-8", errors="replace").strip())
        except json.JSONDecodeError:
            # Не JSON — вероятно boot-баннер MicroPython/REPL-вывод, если
            # порт открылся во время перезапуска агента (см. connect()).
            # Логируем сырую строку, чтобы это было видно, а не выглядело
            # как немой таймаут.
            logger.info(
                "_send_recv(%s): не-JSON ответ, похоже на boot-вывод M5: %r",
                cmd.get("cmd"),
                raw[:200],
            )
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
        return resp is not None and resp.get("ok") is True

    async def relay_set(self, relay: int, state: bool) -> bool:
        """
        Переключить реле.

        :param relay: Номер реле 1..4 (в протоколе агента — поле "ch").
        :param state: True = ON, False = OFF.
        :return: True при успехе.
        """
        resp = await self._cmd({"cmd": "relay_set", "ch": relay, "state": state})
        return resp is not None and resp.get("ok") is True

    async def relay_get(self, relay: int) -> Optional[bool]:
        """
        Прочитать состояние реле.

        :return: True/False или None при ошибке.
        """
        resp = await self._cmd({"cmd": "relay_get", "ch": relay})
        if resp and resp.get("ok") is True:
            return bool(resp.get("state"))
        return None

    async def can_send(self, can_id: int, data: list[int]) -> bool:
        """Отправить CAN-фрейм через M5."""
        resp = await self._cmd({"cmd": "can_send", "id": can_id, "data": data})
        return resp is not None and resp.get("ok") is True

    async def can_recv(self, timeout_ms: int = 500) -> Optional[dict]:
        """
        Принять CAN-фрейм.

        :return: {"id": int, "data": list[int]} или None при таймауте/ошибке.
        """
        resp = await self._cmd({"cmd": "can_recv", "timeout_ms": timeout_ms})
        if resp and resp.get("ok") is True:
            return {"id": resp["id"], "data": resp["data"]}
        return None

    @staticmethod
    def find_port() -> Optional[str]:
        """Найти порт M5StampPLC. None если не найден."""
        return _find_m5_port()
