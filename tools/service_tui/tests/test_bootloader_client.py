"""
test_bootloader_client.py — юнит-тесты BootloaderClient (Фаза 5).

Тестируем чистую логику (парсинг/фильтрация/таймаут) без железа: подменяем
._ser фейковым serial'ом с очередью строк. asyncio.run() вместо pytest-asyncio
(плагин не в зависимостях, а корутины тут короткие) — тот же принцип, что и в
sync-стиле test_flash_backend.py.
"""

from __future__ import annotations

import asyncio

from app.bootloader_client import BootloaderClient


class _FakeSerial:
    """Минимальный stand-in для serial.Serial: отдаёт заранее заданные строки
    (bytes) по одной на readline(), затем b"" (как реальный таймаут readline).
    """

    def __init__(self, lines: list[bytes]) -> None:
        self._lines = list(lines)
        self.written: list[bytes] = []
        self.is_open = True

    def write(self, data: bytes) -> None:
        self.written.append(data)

    def flush(self) -> None:
        pass

    def readline(self) -> bytes:
        return self._lines.pop(0) if self._lines else b""

    def reset_input_buffer(self) -> None:
        pass

    def close(self) -> None:
        self.is_open = False


def _client(lines: list[bytes]) -> BootloaderClient:
    client = BootloaderClient(port="fake")
    client._ser = _FakeSerial(lines)
    return client


# ─── get_smoke_status ───────────────────────────────────────────────────────


def test_smoke_status_pass():
    client = _client([b'{"type":"status","state":"smoke_pass"}\n'])
    assert asyncio.run(client.get_smoke_status(timeout_s=1.0)) is True


def test_smoke_status_fail():
    client = _client([b'{"type":"status","state":"smoke_fail"}\n'])
    assert asyncio.run(client.get_smoke_status(timeout_s=1.0)) is False


def test_smoke_status_ignores_unrelated_status_then_matches():
    """waiting_for_sd/installing — тоже status-эвенты; их надо пропустить и
    дочитать до реального smoke_*."""
    client = _client(
        [
            b'{"type":"status","state":"waiting_for_sd"}\n',
            b'{"type":"pong"}\n',
            b'{"type":"status","state":"smoke_fail"}\n',
        ]
    )
    assert asyncio.run(client.get_smoke_status(timeout_s=1.0)) is False


def test_smoke_status_timeout_returns_none():
    """Прошивка молчит, если результат ещё не закэширован → None (не провал)."""
    client = _client([])
    assert asyncio.run(client.get_smoke_status(timeout_s=0.05)) is None


def test_smoke_status_sends_correct_command():
    client = _client([b'{"type":"status","state":"smoke_pass"}\n'])
    asyncio.run(client.get_smoke_status(timeout_s=1.0))
    assert client._ser.written == [b'{"type":"cmd","cmd":"smoke_status"}\n']


# ─── get_qspi_info ──────────────────────────────────────────────────────────


def test_qspi_info_parsed():
    line = (
        b'{"type":"qspi_info","chip":"W25Q128","mfr":"0xEF",'
        b'"cap_byte":"0x18","size_mb":16,"pass":true}\n'
    )
    result = asyncio.run(_client([line]).get_qspi_info(timeout_s=1.0))
    assert result is not None
    assert result["chip"] == "W25Q128"
    assert result["size_mb"] == 16
    assert result["pass"] is True


def test_qspi_info_ignores_other_events_then_matches():
    client = _client(
        [
            b'{"type":"pong"}\n',
            b'{"type":"qspi_info","chip":"W25Q64","mfr":"0xEF",'
            b'"cap_byte":"0x17","size_mb":8,"pass":false}\n',
        ]
    )
    result = asyncio.run(client.get_qspi_info(timeout_s=1.0))
    assert result["chip"] == "W25Q64"
    assert result["pass"] is False


def test_qspi_info_timeout_returns_none():
    assert asyncio.run(_client([]).get_qspi_info(timeout_s=0.05)) is None
