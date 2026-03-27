#!/usr/bin/env python3
"""
m5_cli.py — интерактивный CLI для ручного тестирования агента M5Stack.

Запуск:
    just host::m5-cli
    # или напрямую:
    uv run --directory tools/hil python m5_cli.py --port /dev/ttyACM1

Использование:
    > ping
    > info
    > power on
    > power off
    > relay 2 on
    > relay 2 off
    > relay_all_off
    > opto 1 on          # активировать EXT_IN1 таргета
    > opto 2 off
    > opto_all_off
    > input 3            # прочитать SYS_IN3 стенда
    > can_send 0x123 01 02 03 04
    > can_recv 1000      # ждать фрейм 1000мс
    > help
    > quit
"""

from __future__ import annotations

import argparse
import json
import sys
import time

import serial

# ---------------------------------------------------------------------------
# Конфигурация
# ---------------------------------------------------------------------------

_DEFAULT_PORT = "/dev/ttyACM1"
_BAUD         = 115_200
_TIMEOUT      = 3.0
_READY_WAIT   = 5.0

# ---------------------------------------------------------------------------
# Цвета
# ---------------------------------------------------------------------------

_GREEN  = "\033[92m"
_RED    = "\033[91m"
_YELLOW = "\033[93m"
_CYAN   = "\033[96m"
_GRAY   = "\033[90m"
_BOLD   = "\033[1m"
_RESET  = "\033[0m"

def _ok(s: str)   -> str: return f"{_GREEN}{s}{_RESET}"
def _err(s: str)  -> str: return f"{_RED}{s}{_RESET}"
def _info(s: str) -> str: return f"{_CYAN}{s}{_RESET}"
def _hint(s: str) -> str: return f"{_GRAY}{s}{_RESET}"

# ---------------------------------------------------------------------------
# Транспорт
# ---------------------------------------------------------------------------

def _send(ser: serial.Serial, cmd: dict) -> dict:
    """Отправить команду и дождаться первого валидного JSON-ответа."""
    line = json.dumps(cmd) + "\r\n"
    ser.reset_input_buffer()
    ser.write(line.encode("ascii"))

    deadline = time.monotonic() + _TIMEOUT
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue

        text = raw.decode("ascii", errors="replace").strip()
        if not text:
            continue

        # Игнорируем служебный стартовый маркер
        if text == "READY":
            continue

        # Игнорируем всё, что не выглядит как JSON-объект
        if not text.startswith("{"):
            continue

        try:
            return json.loads(text)
        except json.JSONDecodeError:
            continue

    raise TimeoutError("Нет корректного JSON-ответа от агента")


def _wait_ready(ser: serial.Serial) -> None:
    """
    Ждём READY от агента.

    Два сценария:
      1. Агент только что запустился (после reset/deploy) → пришлёт READY.
      2. Агент уже работает → READY давно отправлен, не придёт снова.
         Fallback: пробуем ping. Если отвечает — считаем что всё ок.
    """
    print(_info("  Ожидаем READY от агента..."), end=" ", flush=True)
    deadline = time.monotonic() + _READY_WAIT
    while time.monotonic() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if line == "READY":
            print(_ok("OK"))
            return

    # READY не пришёл — агент, вероятно, уже работает. Пробуем ping.
    print(_hint("(не получен, пробуем ping...)"), end=" ", flush=True)
    try:
        resp = _send(ser, {"cmd": "ping"})
        if resp.get("ok"):
            print(_ok("OK (агент уже работал)"))
            return
    except TimeoutError:
        pass

    print(_err("FAILED"))
    print(_err("  Агент не отвечает. Проверьте порт и задеплойте агент:"))
    print(_hint("    just host::m5-deploy"))
    sys.exit(1)


# ---------------------------------------------------------------------------
# Парсер команд
# ---------------------------------------------------------------------------

def _parse_and_run(ser: serial.Serial, line: str) -> bool:
    """
    Разобрать строку, выполнить команду.
    Вернуть False если нужно выйти.
    """
    parts = line.strip().split()
    if not parts:
        return True
    cmd = parts[0].lower()

    # ── quit / exit ──────────────────────────────────────────────────────────
    if cmd in ("quit", "exit", "q"):
        return False

    # ── help ─────────────────────────────────────────────────────────────────
    if cmd == "help":
        print(_hint("""
  Команды:
    ping                       проверить связь с агентом
    info                       версия, статус CAN, состояние реле
    power on|off               питание таргета (RLY1)
    relay <1-4> on|off         прямое управление реле
    relay_all_off              выключить все реле
    opto <1-3> on|off          активировать оптовход таргета
    opto_all_off               деактивировать все оптовходы
    input <1-8>                прочитать входной канал стенда
    can_send <id> [байты...]   отправить CAN фрейм (id в hex или dec)
    can_recv [timeout_ms]      принять CAN фрейм
    raw <json>                 отправить произвольный JSON напрямую
    help                       эта справка
    quit / exit / q            выход
        """))
        return True

    # ── ping ─────────────────────────────────────────────────────────────────
    if cmd == "ping":
        _run(ser, {"cmd": "ping"})
        return True

    # ── info ─────────────────────────────────────────────────────────────────
    if cmd == "info":
        resp = _run(ser, {"cmd": "info"})
        if resp and resp.get("ok"):
            print(f"    fw:      {resp.get('fw')}")
            print(f"    python:  {resp.get('python', '?')[:40]}")
            print(f"    can:     {'✅ доступен' if resp.get('can_ok') else '❌ недоступен'}")
            relays = resp.get("relays", [])
            for i, state in enumerate(relays, 1):
                mark = "●" if state else "○"
                print(f"    RLY{i}:    {mark} {'ON ' if state else 'off'}")
        return True

    # ── power ─────────────────────────────────────────────────────────────────
    if cmd == "power":
        if len(parts) < 2 or parts[1].lower() not in ("on", "off"):
            print(_err("  Использование: power on|off"))
            return True
        state = parts[1].lower() == "on"
        _run(ser, {"cmd": "power", "state": state})
        return True

    # ── relay ─────────────────────────────────────────────────────────────────
    if cmd == "relay":
        if len(parts) < 3:
            print(_err("  Использование: relay <1-4> on|off"))
            return True
        try:
            ch = int(parts[1])
        except ValueError:
            print(_err("  ch должен быть числом 1-4"))
            return True
        if parts[2].lower() not in ("on", "off"):
            print(_err("  Состояние: on или off"))
            return True
        state = parts[2].lower() == "on"
        _run(ser, {"cmd": "relay_set", "ch": ch, "state": state})
        return True

    # ── relay_all_off ─────────────────────────────────────────────────────────
    if cmd == "relay_all_off":
        _run(ser, {"cmd": "relay_all_off"})
        return True

    # ── opto ──────────────────────────────────────────────────────────────────
    if cmd == "opto":
        if len(parts) < 3:
            print(_err("  Использование: opto <1-3> on|off"))
            return True
        try:
            ch = int(parts[1])
        except ValueError:
            print(_err("  ch должен быть числом 1-3"))
            return True
        if parts[2].lower() not in ("on", "off"):
            print(_err("  Состояние: on или off"))
            return True
        state = parts[2].lower() == "on"
        _run(ser, {"cmd": "opto_set", "ch": ch, "state": state})
        return True

    # ── opto_all_off ──────────────────────────────────────────────────────────
    if cmd == "opto_all_off":
        _run(ser, {"cmd": "opto_all_off"})
        return True

    # ── input ─────────────────────────────────────────────────────────────────
    if cmd == "input":
        if len(parts) < 2:
            print(_err("  Использование: input <1-8>"))
            return True
        try:
            ch = int(parts[1])
        except ValueError:
            print(_err("  ch должен быть числом 1-8"))
            return True
        resp = _run(ser, {"cmd": "input_read", "ch": ch})
        if resp and resp.get("ok"):
            state = resp.get("state", False)
            mark  = _ok("ACTIVE") if state else _hint("inactive")
            print(f"    SYS_IN{ch}: {mark}")
        return True

    # ── can_send ──────────────────────────────────────────────────────────────
    if cmd == "can_send":
        if len(parts) < 2:
            print(_err("  Использование: can_send <id_hex> [байт байт ...]"))
            return True
        try:
            frame_id = int(parts[1], 0)   # 0x123 или 291
            data     = [int(b, 0) for b in parts[2:]]
        except ValueError as exc:
            print(_err(f"  Ошибка парсинга: {exc}"))
            return True
        _run(ser, {"cmd": "can_send", "id": frame_id, "data": data})
        return True

    # ── can_recv ──────────────────────────────────────────────────────────────
    if cmd == "can_recv":
        timeout_ms = int(parts[1]) if len(parts) > 1 else 500
        resp = _run(ser, {"cmd": "can_recv", "timeout_ms": timeout_ms})
        if resp and resp.get("ok"):
            frame_id = resp.get("id", 0)
            data     = resp.get("data", [])
            ext      = resp.get("ext", False)
            data_hex = " ".join(f"{b:02X}" for b in data)
            print(f"    id=0x{frame_id:X}  ext={ext}  data=[{data_hex}]")
        return True

    # ── raw ───────────────────────────────────────────────────────────────────
    if cmd == "raw":
        raw_json = line.strip()[4:].strip()
        if not raw_json:
            print(_err("  Использование: raw {\"cmd\": \"...\"}"))
            return True
        try:
            payload = json.loads(raw_json)
        except json.JSONDecodeError as exc:
            print(_err(f"  JSON ошибка: {exc}"))
            return True
        _run(ser, payload)
        return True

    print(_err(f"  Неизвестная команда: {cmd!r}  (введите help)"))
    return True


def _run(ser: serial.Serial, cmd: dict) -> dict | None:
    """Выполнить команду и красиво распечатать результат."""
    print(_hint(f"  → {json.dumps(cmd)}"))
    try:
        resp = _send(ser, cmd)
    except TimeoutError as exc:
        print(_err(f"  ✗ {exc}"))
        return None
    except Exception as exc:  # noqa: BLE001
        print(_err(f"  ✗ {exc}"))
        return None

    if resp.get("ok"):
        # Убираем ok=true из вывода — это шум
        display = {k: v for k, v in resp.items() if k != "ok"}
        if display:
            print(_ok(f"  ✓ {json.dumps(display, ensure_ascii=False)}"))
        else:
            print(_ok("  ✓ ok"))
    else:
        print(_err(f"  ✗ err: {resp.get('err', '?')}"))

    return resp


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="M5Stack StamPLC CLI")
    parser.add_argument("--port", default=_DEFAULT_PORT, help="COM-порт")
    parser.add_argument("--baud", type=int, default=_BAUD)
    args = parser.parse_args()

    print()
    print(_bold("  M5Stack StamPLC CLI"))
    print(_hint(f"  Порт: {args.port} @ {args.baud} baud"))
    print(_hint("  Введите 'help' для списка команд, 'quit' для выхода"))
    print()

    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            timeout=_TIMEOUT,
            write_timeout=1.0,
        )
    except serial.SerialException as exc:
        print(_err(f"  Не удалось открыть порт {args.port}: {exc}"))
        print(_hint("  Проверьте: ls /dev/tty*  или  just host::m5-scan"))
        sys.exit(1)

    _wait_ready(ser)
    print()

    # Автоматически запрашиваем info при старте
    _parse_and_run(ser, "info")
    print()

    try:
        while True:
            try:
                line = input(_bold("  m5-hil-agent> "))
            except EOFError:
                break
            except KeyboardInterrupt:
                print()
                break
            if not _parse_and_run(ser, line):
                break
    finally:
        # При выходе — безопасно выключить все реле
        print()
        print(_hint("  Выключаем все реле..."))
        try:
            _send(ser, {"cmd": "relay_all_off"})
            print(_ok("  ✓ Все реле выключены"))
        except Exception:
            pass
        ser.close()
        print(_hint("  Порт закрыт. До свидания!"))
        print()


def _bold(s: str) -> str:
    return f"{_BOLD}{s}{_RESET}"


if __name__ == "__main__":
    main()