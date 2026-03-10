#!/usr/bin/env python3
"""
dcd_cfg_to_bin.py — конвертация DCD .cfg (NXP Config Tools) в .bin (BootROM формат)

Формат DCD бинарника (IMX RT RM, Chapter 9.7):
  [Header]
    0xD2        — DCD tag
    length[1:0] — общая длина в байтах (big-endian, 2 байта)
    0x41        — version

  [Commands] — последовательно:
    Write Data (0xCC):
      0xCC tag, length BE16, parameter (ширина: 4=word)
      [addr BE32, value BE32] × N  — группируются подряд идущие записи

    Check Bits Set (0xCF) — wait_until (expr & mask) != 0:
      0xCF tag, 0x000C length, parameter (4=word)
      addr BE32, mask BE32, count BE32 (0 = бесконечно)

Использование:
    python3 dcd_cfg_to_bin.py dcd.cfg dcd.bin
    python3 dcd_cfg_to_bin.py dcd.cfg          # выведет dcd.bin рядом с cfg
"""

import re
import struct
import sys
from pathlib import Path
from dataclasses import dataclass, field
from typing import Union

# ─── Константы DCD ────────────────────────────────────────────────────────────
DCD_TAG          = 0xD2
DCD_VERSION      = 0x41
CMD_WRITE        = 0xCC
CMD_CHECK_SET    = 0xCF   # wait_until (reg & mask) != 0
CMD_CHECK_CLEAR  = 0xD0   # wait_until (reg & mask) == 0
WIDTH_WORD       = 0x04   # 32-bit


# ─── Типы команд ──────────────────────────────────────────────────────────────
@dataclass
class WriteEntry:
    addr: int
    value: int


@dataclass
class WriteCmd:
    entries: list[WriteEntry] = field(default_factory=list)

    def to_bytes(self) -> bytes:
        length = 4 + len(self.entries) * 8
        # NXP RM format: Tag(1) | Length(2 BE) | Parameter(1)
        hdr = struct.pack(">BHB", CMD_WRITE, length, WIDTH_WORD)
        data = b"".join(
            struct.pack(">II", e.addr, e.value) for e in self.entries
        )
        return hdr + data


@dataclass
class CheckCmd:
    addr: int
    mask: int
    check_set: bool = True   # True = wait != 0, False = wait == 0

    def to_bytes(self) -> bytes:
        tag = CMD_CHECK_SET if self.check_set else CMD_CHECK_CLEAR
        # NXP RM format: Tag(1) | Length(2 BE=12) | Parameter(1) | Addr(4) | Mask(4)
        # Parameter: bits[2:0]=width(4=32bit), bit[3]=check_set → 0x1C / check_clear → 0x14
        # No count field — poll indefinitely is the default (length=12, not 16)
        param = 0x1C if self.check_set else 0x14
        length = 12  # 4 header + 4 addr + 4 mask
        return struct.pack(">BHBII", tag, length, param, self.addr, self.mask)


# ─── Парсер .cfg ─────────────────────────────────────────────────────────────
def parse_hex(s: str) -> int:
    return int(s, 16) if s.startswith("0x") or s.startswith("0X") else int(s)


def parse_cfg(text: str) -> list[Union[WriteCmd, CheckCmd]]:
    """
    Парсит .cfg и возвращает список команд DCD.
    Соседние write-записи группируются в одну WriteCmd.
    """
    # Убираем блочные комментарии /* ... */
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    # Убираем строчные комментарии // ...
    text = re.sub(r"//[^\n]*", "", text)

    commands: list[Union[WriteCmd, CheckCmd]] = []
    current_write: WriteCmd | None = None

    # Паттерн: *(uint32_t*)0xADDR = 0xVALUE;
    re_write = re.compile(
        r"\*\s*\(\s*uint32_t\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;"
    )

    # Паттерн: (*(uint32_t*)0xADDR & 0xMASK) != 0;
    re_check_set = re.compile(
        r"\(\s*\*\s*\(\s*uint32_t\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)\s*&\s*(0x[0-9A-Fa-f]+)\s*\)\s*!=\s*0\s*;"
    )

    # Паттерн: (*(uint32_t*)0xADDR & 0xMASK) == 0;
    re_check_clear = re.compile(
        r"\(\s*\*\s*\(\s*uint32_t\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)\s*&\s*(0x[0-9A-Fa-f]+)\s*\)\s*==\s*0\s*;"
    )

    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue

        m = re_write.search(line)
        if m:
            addr  = parse_hex(m.group(1))
            value = parse_hex(m.group(2))
            if current_write is None:
                current_write = WriteCmd()
                commands.append(current_write)
            current_write.entries.append(WriteEntry(addr, value))
            continue

        m = re_check_set.search(line)
        if m:
            if current_write is not None:
                current_write = None  # разрываем группу write
            addr = parse_hex(m.group(1))
            mask = parse_hex(m.group(2))
            commands.append(CheckCmd(addr, mask, check_set=True))
            continue

        m = re_check_clear.search(line)
        if m:
            if current_write is not None:
                current_write = None
            addr = parse_hex(m.group(1))
            mask = parse_hex(m.group(2))
            commands.append(CheckCmd(addr, mask, check_set=False))
            continue

        # Не write и не check — разрываем текущую write-группу
        # (на случай если между записями есть другой синтаксис)
        if current_write is not None:
            current_write = None

    return commands


# ─── Сборка бинарника ─────────────────────────────────────────────────────────
def build_dcd_binary(commands: list[Union[WriteCmd, CheckCmd]]) -> bytes:
    body = b"".join(cmd.to_bytes() for cmd in commands)
    total_length = 4 + len(body)  # 4 байта header
    # NXP RM format: Tag(1) | Length(2 BE) | Version(1)
    header = struct.pack(">BHB", DCD_TAG, total_length, DCD_VERSION)
    return header + body


# ─── Отчёт ────────────────────────────────────────────────────────────────────
def print_report(commands: list[Union[WriteCmd, CheckCmd]], out_size: int) -> None:
    writes = sum(1 for c in commands if isinstance(c, WriteCmd))
    checks = sum(1 for c in commands if isinstance(c, CheckCmd))
    entries = sum(len(c.entries) for c in commands if isinstance(c, WriteCmd))

    print(f"\n{'─'*52}")
    print(f"  DCD конвертация завершена")
    print(f"{'─'*52}")
    print(f"  Write команд (групп) : {writes}")
    print(f"  Write записей всего  : {entries}")
    print(f"  Check команд         : {checks}")
    print(f"  Размер бинарника     : {out_size} байт (0x{out_size:04X})")
    print(f"{'─'*52}")

    print("\n  Команды:")
    for i, cmd in enumerate(commands):
        if isinstance(cmd, WriteCmd):
            print(f"  [{i:02d}] WRITE  × {len(cmd.entries):3d} entries"
                  f"  (0x{cmd.entries[0].addr:08X} … 0x{cmd.entries[-1].addr:08X})")
        else:
            kind = "CHECK_SET  (!= 0)" if cmd.check_set else "CHECK_CLR  (== 0)"
            print(f"  [{i:02d}] {kind}  addr=0x{cmd.addr:08X}  mask=0x{cmd.mask:08X}")
    print()


# ─── main ─────────────────────────────────────────────────────────────────────
def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    cfg_path = Path(sys.argv[1])
    if not cfg_path.exists():
        print(f"[ERROR] Файл не найден: {cfg_path}")
        sys.exit(1)

    out_path = Path(sys.argv[2]) if len(sys.argv) >= 3 else cfg_path.with_suffix(".bin")

    text = cfg_path.read_text(encoding="utf-8", errors="replace")
    commands = parse_cfg(text)

    if not commands:
        print("[ERROR] Ни одной команды не найдено. Проверь формат .cfg файла.")
        sys.exit(1)

    binary = build_dcd_binary(commands)
    out_path.write_bytes(binary)

    print(f"[OK] Входной файл : {cfg_path}")
    print(f"[OK] Выходной файл: {out_path}")
    print_report(commands, len(binary))


if __name__ == "__main__":
    main()