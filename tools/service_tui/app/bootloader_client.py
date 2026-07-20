"""
bootloader_client.py — async CDC-клиент загрузчика (Фаза 5).

Тонкий подкласс FirmwareClient: загрузчик и firmware_test делят один и тот же
JSON-lines v2 протокол и один VID:PID (намеренно, чтобы переиспользовать
клиентский код), поэтому транспорт, ping() и get_version() наследуются как есть.

Отличие — набор команд. У загрузчика ЕСТЬ smoke_status/qspi_info (Фаза 4:
результаты SDRAM/SEMC smoke-теста и идентификации QSPI-чипа, закэшированные на
раннем старте main), но НЕТ list_tests/run_selected/get_uid — те специфичны для
firmware_test. Поэтому подкласс только ДОБАВЛЯЕТ методы, ничего не убирая
(вызывать унаследованные list_tests() и т.п. на загрузчике просто бессмысленно —
он на них не ответит, вернётся пустой список по таймауту).

Используется экраном верификации (Тир-1) сразу после серийной прошивки
загрузчика: оператор переводит BOOT_MOD → GND + reset, плата грузится в
загрузчик, поднимает USB CDC, и service-tui читает ping/version/smoke/qspi.
"""

from __future__ import annotations

import asyncio
from typing import Callable, Optional

from .firmware_client import FirmwareClient

# Загрузчик шлёт status/qspi_info только если результат УЖЕ закэширован (см.
# protocol_send_smoke_status/protocol_send_qspi_info в прошивке — при
# неизвестном результате ответа нет вообще). К моменту подключения по CDC
# smoke обычно уже прогнан в раннем main, но таймаут страхует от гонки и от
# старой прошивки без этих команд — трактуется как «неизвестно» (None).
_SMOKE_TIMEOUT_S = 3.0
_QSPI_TIMEOUT_S = 3.0


class BootloaderClient(FirmwareClient):
    """CDC-клиент загрузчика: ping/get_version (унаследованы) + smoke/qspi."""

    async def connect(self) -> None:
        """Открыть порт и проверить связь через ping→pong.

        Копия FirmwareClient.connect() с сообщением про загрузчик — в
        QC-контексте «firmware_test не отвечает» вводило бы оператора в
        заблуждение (проверяем-то загрузчик). Транспорт/ping идентичны.
        """
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._open)
        if not await self.ping():
            await self.disconnect()
            raise ConnectionError(f"Загрузчик не отвечает на ping: {self._port}")

    async def get_smoke_status(
        self, timeout_s: float = _SMOKE_TIMEOUT_S
    ) -> Optional[bool]:
        """Запросить результат SDRAM/SEMC smoke-теста.

        :return: True (smoke_pass) / False (smoke_fail) / None — загрузчик не
                 ответил за timeout_s (результат ещё не закэширован либо старая
                 прошивка без команды). None ≠ провал: это «неизвестно».
        """

        def _match(ev: dict) -> Optional[bool]:
            # Загрузчик отвечает обобщённым status-эвентом; фильтруем именно
            # smoke_*, чтобы не спутать с waiting_for_sd/installing, которые
            # main может слать в том же потоке.
            if ev.get("type") == "status":
                state = ev.get("state")
                if state == "smoke_pass":
                    return True
                if state == "smoke_fail":
                    return False
            return None

        return await self._query_event("smoke_status", _match, timeout_s)

    async def get_qspi_info(
        self, timeout_s: float = _QSPI_TIMEOUT_S
    ) -> Optional[dict]:
        """Запросить идентификацию QSPI-чипа.

        :return: dict с полями chip/mfr/cap_byte/size_mb/pass (as-is с прошивки),
                 либо None по таймауту (см. get_smoke_status про None).
        """

        def _match(ev: dict) -> Optional[dict]:
            return ev if ev.get("type") == "qspi_info" else None

        return await self._query_event("qspi_info", _match, timeout_s)

    async def _query_event(
        self,
        cmd: str,
        match: Callable[[dict], Optional[object]],
        timeout_s: float,
    ) -> Optional[object]:
        """Отправить {"type":"cmd","cmd":cmd} и вернуть match() первого
        подходящего события, либо None по таймауту.

        match(event) → не-None значение при совпадении (читаем дальше, пока
        None). Неподходящие события (pong, чужой status, эхо прошлой команды)
        молча пропускаются — тот же паттерн read-loop, что в ping(), но с
        предикатом вместо фиксированного типа.
        """
        await self._send({"type": "cmd", "cmd": cmd})
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout_s
        while loop.time() < deadline:
            event = await loop.run_in_executor(None, self._read_line)
            if event is not None:
                result = match(event)
                if result is not None:
                    return result
            await asyncio.sleep(0)
        return None
