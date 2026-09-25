#!/usr/bin/env python3
"""dcd_tool — конвертер и валидатор DCD (Device Configuration Data) для i.MX RT1050.

Источник истины DCD — DCD Tool в MCUXpresso Config Tools (отдельный .mex),
который генерирует dcd.c (c_array). Этот скрипт превращает его в то, что
нужно потребителям:

    dcd.c (Config Tools) ─┬─► dcd.bin         → nxpimage hab (DCDFilePath), BootROM
    dcd.bin (legacy)      ├─► <name>.c        → const-массив для bsp_sdram_run_dcd()
    dcd.txt (варианты)    └─► dcd.txt         → читаемый листинг для ревью/diff

Формат и семантика — i.MX RT1050 RM Rev.4 §9.7.2 (табл. 9-41, 9-45).
Правила валидации совпадают с bsp/sdram/src/dcd_exec.c.

Словарь команд — как в Config Tools (write_value, write_set_bits,
check_any_bit_set, …), а не SPSDK: в SPSDK 3.7 имена check-операций для
par-флагов 0x08/0x10 (CheckAllSet/CheckAnyClear) расходятся с RM табл. 9-45
(0x10 = all set, 0x08 = any clear; так же в u-boot imximage.h). SPSDK здесь
используется только для побайтовой перекрёстной проверки в тестах.

Только стандартная библиотека Python — можно звать из CMake/контейнера без venv.

Использование:
    dcd_tool.py INPUT [--bin OUT] [--c OUT --symbol NAME] [--txt OUT]
                      [--compare FILE] [--werror]

    INPUT — .c (любой C-массив, берётся первый; Config Tools dcd.c подходит),
            .bin (сырой DCD) или .txt (текстовый формат, см. ниже).

Текстовый формат (по строке на пару/команду, '#' — комментарий):
    write_value      4 0x402F0000 0x10000004
    write_clear_bits 4 0x400FC014 0x00000040
    write_set_bits   4 0x400FC014 0x00000040
    check_any_bit_set 4 0x402F003C 0x00000001 [count]
    nop
Подряд идущие write-строки с одинаковыми операцией и шириной сливаются в одну
команду; пустая строка разрывает команду (так же выглядит вывод --txt).
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, Union

# ── Формат (RM §9.7.2) ──────────────────────────────────────────────────────

TAG_HEADER = 0xD2
TAG_WRITE = 0xCC
TAG_CHECK = 0xCF
TAG_NOP = 0xC0
VERSION = 0x41
MAX_SIZE = 1768
HDR_SIZE = 4
WIDTHS = (1, 2, 4)

# flags = (par >> 3) & 3:  bit0 — "Mask" (par bit3), bit1 — "Set" (par bit4)
WRITE_NAMES = {
    0: "write_value",  # *addr = val
    1: "write_clear_bits",  # *addr &= ~val
    2: "write_value_set",  # *addr = val (Mask=0 Set=1 — по RM тоже запись значения)
    3: "write_set_bits",  # *addr |= val
}
CHECK_NAMES = {
    0: "check_all_bits_clear",  # (*addr & mask) == 0
    1: "check_any_bit_clear",  # (*addr & mask) != mask
    2: "check_all_bits_set",  # (*addr & mask) == mask
    3: "check_any_bit_set",  # (*addr & mask) != 0
}
WRITE_BY_NAME = {v: k for k, v in WRITE_NAMES.items()}
CHECK_BY_NAME = {v: k for k, v in CHECK_NAMES.items()}

WRITE_ACTION = {0: "= val", 1: "&= ~val", 2: "= val", 3: "|= val"}
CHECK_ACTION = {0: "& m == 0", 1: "& m != m", 2: "& m == m", 3: "& m != 0"}

# Разрешённые BootROM адреса записи — RM табл. 9-42.
ROM_WRITE_RANGES = (
    (0x400A4000, 0x400A7FFF, "IOMUXC_SNVS_GPR"),
    (0x400A8000, 0x400ABFFF, "IOMUXC_SNVS"),
    (0x400AC000, 0x400AFFFF, "IOMUXC_GPR"),
    (0x400D8000, 0x400DBFFF, "CCM_ANALOG"),
    (0x400FC000, 0x400FFFFF, "CCM"),
    (0x402F0000, 0x402F3FFF, "SEMC"),
)
# Нет в табл. 9-42, но пишется DCD-ом EVK от NXP (мукс/пады EMC) — BootROM принимает.
ROM_WRITE_RANGES_EVK = ((0x401F8000, 0x401FBFFF, "IOMUXC"),)


class DcdError(Exception):
    """Невалидный DCD."""


@dataclass
class Write:
    width: int
    flags: int
    pairs: list[tuple[int, int]] = field(default_factory=list)


@dataclass
class Check:
    width: int
    flags: int
    addr: int
    mask: int
    count: Optional[int] = None


@dataclass
class Nop:
    pass


Command = Union[Write, Check, Nop]


# ── Валидация доступа ────────────────────────────────────────────────────────


def _check_access(addr: int, value: int, width: int, where: str) -> None:
    if width not in WIDTHS:
        raise DcdError(f"{where}: ширина {width} (допустимо 1/2/4)")
    if addr % width:
        raise DcdError(f"{where}: адрес 0x{addr:08X} не выровнен по {width}")
    if width < 4 and (value >> (width * 8)):
        raise DcdError(f"{where}: значение 0x{value:08X} шире {width} байт")
    if not (0 <= addr <= 0xFFFFFFFF and 0 <= value <= 0xFFFFFFFF):
        raise DcdError(f"{where}: адрес/значение вне 32 бит")


# ── Бинарь ⇄ команды ─────────────────────────────────────────────────────────


def parse_bin(data: bytes) -> list[Command]:
    """Разобрать и провалидировать DCD (правила — как dcd_exec_validate())."""
    if len(data) < HDR_SIZE or data[0] != TAG_HEADER or data[3] != VERSION:
        raise DcdError("заголовок: ожидается D2 xx xx 41")
    length = struct.unpack(">H", data[1:3])[0]
    if not HDR_SIZE <= length <= MAX_SIZE or length > len(data):
        raise DcdError(f"заголовок: длина {length} (буфер {len(data)}, предел {MAX_SIZE})")

    cmds: list[Command] = []
    off = HDR_SIZE
    while off < length:
        if length - off < HDR_SIZE:
            raise DcdError(f"@{off}: обрезанный заголовок команды")
        tag, clen, par = data[off], struct.unpack(">H", data[off + 1 : off + 3])[0], data[off + 3]
        if clen < HDR_SIZE or clen > length - off:
            raise DcdError(f"@{off}: длина команды {clen} выходит за DCD")
        body = data[off + HDR_SIZE : off + clen]
        width, flags = par & 0x7, (par >> 3) & 0x3
        where = f"@{off}"
        if tag == TAG_WRITE:
            if not body or len(body) % 8:
                raise DcdError(f"{where}: write без пар или с неполной парой")
            pairs = [struct.unpack(">II", body[i : i + 8]) for i in range(0, len(body), 8)]
            for addr, value in pairs:
                _check_access(addr, value, width, where)
            cmds.append(Write(width, flags, list(pairs)))
        elif tag == TAG_CHECK:
            if len(body) not in (8, 12):
                raise DcdError(f"{where}: check длиной {clen} (допустимо 12/16)")
            addr, mask = struct.unpack(">II", body[:8])
            _check_access(addr, mask, width, where)
            count = struct.unpack(">I", body[8:12])[0] if len(body) == 12 else None
            cmds.append(Check(width, flags, addr, mask, count))
        elif tag == TAG_NOP:
            if clen != HDR_SIZE:
                raise DcdError(f"{where}: NOP длиной {clen}")
            cmds.append(Nop())
        else:
            raise DcdError(f"{where}: неподдерживаемая команда 0x{tag:02X}")
        off += clen
    return cmds


def encode(cmds: list[Command]) -> bytes:
    """Собрать DCD из команд (с той же валидацией, что при разборе)."""
    out = bytearray()
    for n, cmd in enumerate(cmds):
        where = f"команда #{n + 1}"
        if isinstance(cmd, Write):
            if not cmd.pairs:
                raise DcdError(f"{where}: write без пар")
            for addr, value in cmd.pairs:
                _check_access(addr, value, cmd.width, where)
            par = (cmd.flags << 3) | cmd.width
            out += struct.pack(">BHB", TAG_WRITE, HDR_SIZE + 8 * len(cmd.pairs), par)
            for addr, value in cmd.pairs:
                out += struct.pack(">II", addr, value)
        elif isinstance(cmd, Check):
            _check_access(cmd.addr, cmd.mask, cmd.width, where)
            par = (cmd.flags << 3) | cmd.width
            clen = 16 if cmd.count is not None else 12
            out += struct.pack(">BHBII", TAG_CHECK, clen, par, cmd.addr, cmd.mask)
            if cmd.count is not None:
                out += struct.pack(">I", cmd.count)
        else:
            out += struct.pack(">BHB", TAG_NOP, HDR_SIZE, 0)
    total = HDR_SIZE + len(out)
    if total > MAX_SIZE:
        raise DcdError(f"DCD {total} байт превышает предел BootROM {MAX_SIZE}")
    return struct.pack(">BHB", TAG_HEADER, total, VERSION) + bytes(out)


# ── Текст ⇄ команды ──────────────────────────────────────────────────────────


def _int(token: str, where: str) -> int:
    try:
        return int(token, 0)
    except ValueError as exc:
        raise DcdError(f"{where}: не число '{token}'") from exc


def parse_txt(text: str) -> list[Command]:
    cmds: list[Command] = []
    open_write: Optional[Write] = None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        where = f"строка {lineno}"
        if not line:
            open_write = None  # пустая строка разрывает write-команду
            continue
        tok = line.split()
        name = tok[0].lower()
        if name == "nop":
            cmds.append(Nop())
            open_write = None
        elif name in WRITE_BY_NAME:
            if len(tok) != 4:
                raise DcdError(f"{where}: ожидается '{name} <ширина> <адрес> <значение>'")
            width, addr, value = (_int(t, where) for t in tok[1:])
            _check_access(addr, value, width, where)
            flags = WRITE_BY_NAME[name]
            if open_write is None or open_write.flags != flags or open_write.width != width:
                open_write = Write(width, flags)
                cmds.append(open_write)
            open_write.pairs.append((addr, value))
        elif name in CHECK_BY_NAME:
            if len(tok) not in (4, 5):
                raise DcdError(f"{where}: ожидается '{name} <ширина> <адрес> <маска> [count]'")
            width, addr, mask = (_int(t, where) for t in tok[1:4])
            count = _int(tok[4], where) if len(tok) == 5 else None
            _check_access(addr, mask, width, where)
            cmds.append(Check(width, CHECK_BY_NAME[name], addr, mask, count))
            open_write = None
        else:
            raise DcdError(f"{where}: неизвестная команда '{tok[0]}'")
    return cmds


def to_txt(cmds: list[Command]) -> str:
    blocks = []
    for cmd in cmds:
        if isinstance(cmd, Write):
            name = WRITE_NAMES[cmd.flags]
            blocks.append(
                "\n".join(f"{name} {cmd.width} 0x{a:08X} 0x{v:08X}" for a, v in cmd.pairs)
            )
        elif isinstance(cmd, Check):
            line = f"{CHECK_NAMES[cmd.flags]} {cmd.width} 0x{cmd.addr:08X} 0x{cmd.mask:08X}"
            blocks.append(line + (f" {cmd.count}" if cmd.count is not None else ""))
        else:
            blocks.append("nop")
    return "\n\n".join(blocks) + "\n"


# ── C ⇄ байты ────────────────────────────────────────────────────────────────

_C_COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
_C_ARRAY = re.compile(r"\[\s*\w*\s*\]\s*=\s*\{(.*?)\}", re.S)


def parse_c_array(source: str) -> bytes:
    """Взять первый C-массив вида `name[] = { ... }` (Config Tools dcd.c или наш вывод)."""
    match = _C_ARRAY.search(_C_COMMENT.sub(" ", source))
    if match is None:
        raise DcdError("в C-файле не найден массив 'name[] = { ... }'")
    tokens = [t.strip() for t in match.group(1).split(",") if t.strip()]
    try:
        values = [int(t.rstrip("uU"), 0) for t in tokens]
    except ValueError as exc:
        raise DcdError(f"в C-массиве не число: {exc}") from exc
    if any(not 0 <= v <= 0xFF for v in values):
        raise DcdError("в C-массиве значение вне 0..255")
    return bytes(values)


def to_c(cmds: list[Command], data: bytes, symbol: str, source: str) -> str:
    lines = [
        "/*",
        f" * Сгенерировано tools/host/dcd_tool.py из {source}. Не редактировать —",
        " * источник истины: DCD Tool (Config Tools). Исполняется bsp_sdram_run_dcd().",
        " */",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"const uint8_t {symbol}[] = {{",
        f"    /* header: tag D2, length {len(data)}, version 41 */",
        "    " + ", ".join(f"0x{b:02X}" for b in data[:HDR_SIZE]) + ",",
    ]
    off = HDR_SIZE
    for cmd in cmds:
        chunk = encode([cmd])[HDR_SIZE:]
        lines.append(f"    /* {_describe(cmd)} */")
        for i in range(0, len(chunk), 12):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk[i : i + 12]) + ",")
        off += len(chunk)
    lines += ["};", "", f"const size_t {symbol}_size = sizeof({symbol});", ""]
    return "\n".join(lines)


def _describe(cmd: Command) -> str:
    if isinstance(cmd, Write):
        head = f"{WRITE_NAMES[cmd.flags]} w{cmd.width} (*addr {WRITE_ACTION[cmd.flags]})"
        if len(cmd.pairs) == 1:
            addr, value = cmd.pairs[0]
            return f"{head}: 0x{addr:08X} <- 0x{value:08X}"
        return f"{head}: {len(cmd.pairs)} пар, 0x{cmd.pairs[0][0]:08X}.."
    if isinstance(cmd, Check):
        cnt = f", count {cmd.count}" if cmd.count is not None else ""
        return (
            f"{CHECK_NAMES[cmd.flags]} w{cmd.width}: 0x{cmd.addr:08X} "
            f"{CHECK_ACTION[cmd.flags]}, m=0x{cmd.mask:08X}{cnt}"
        )
    return "nop"


# ── Проверка адресов по RM табл. 9-42 ────────────────────────────────────────


def rom_address_warnings(cmds: list[Command]) -> list[str]:
    """Записи вне разрешённых BootROM диапазонов (RM табл. 9-42 + IOMUXC как у EVK)."""
    allowed = ROM_WRITE_RANGES + ROM_WRITE_RANGES_EVK
    out = []
    for cmd in cmds:
        if not isinstance(cmd, Write):
            continue
        for addr, _ in cmd.pairs:
            if not any(lo <= addr <= hi for lo, hi, _ in allowed):
                out.append(
                    f"запись в 0x{addr:08X} вне RM табл. 9-42 — BootROM может отвергнуть команду"
                )
    return out


# ── Проверка PFD (RM: CCM_ANALOG_PFD_480/528) ─────────────────────────────────

# Регистр → (имя, значение после сброса по RM). Байт n — PFDn: FRAC [5:0], STABLE [6], CLKGATE [7].
PFD_REGS = {
    0x400D80F0: ("PFD_480", 0x0F1A231B),
    0x400D8100: ("PFD_528", 0x1018101B),
}
PFD_FRAC_MIN, PFD_FRAC_MAX = 12, 35


def pfd_frac_errors(cmds: list[Command]) -> list[str]:
    """FRAC открытого PFD вне 12…35 после DCD: PFD не даёт такта (так висел SEMC на FRAC=36).

    Смотрим итоговое состояние (clear+set по шагам проходит через FRAC=0) и только PFD,
    чьи FRAC/CLKGATE DCD трогал ненулевым значением. Обнуление FRAC записью значения
    (legacy: PFD_528=0x00230000 → PFD0/1/3=0) не ловится — известно, SEMC_TIMING.md.
    """
    regs = {addr: reset for addr, (_, reset) in PFD_REGS.items()}
    touched: dict[int, set[int]] = {addr: set() for addr in PFD_REGS}
    for cmd in cmds:
        if not isinstance(cmd, Write):
            continue
        for addr, value in cmd.pairs:
            if addr not in regs:
                continue
            cur = regs[addr]
            regs[addr] = {0: value, 2: value, 1: cur & ~value, 3: cur | value}[cmd.flags]
            for n in range(4):
                if cmd.flags in (0, 2):
                    if (value >> (8 * n)) & 0x3F:
                        touched[addr].add(n)
                elif (value >> (8 * n)) & 0xBF:
                    touched[addr].add(n)
    out = []
    for addr, (name, _) in PFD_REGS.items():
        for n in sorted(touched[addr]):
            field_ = (regs[addr] >> (8 * n)) & 0xFF
            frac = field_ & 0x3F
            if not field_ & 0x80 and not PFD_FRAC_MIN <= frac <= PFD_FRAC_MAX:
                out.append(
                    f"{name}.PFD{n}_FRAC = {frac} вне {PFD_FRAC_MIN}…{PFD_FRAC_MAX} "
                    f"(RM, CCM_ANALOG_{name}) — PFD не даёт такта"
                )
    return out


def summary(cmds: list[Command], data: bytes) -> str:
    writes = sum(len(c.pairs) for c in cmds if isinstance(c, Write))
    checks = sum(isinstance(c, Check) for c in cmds)
    return f"{len(data)} байт, {len(cmds)} команд, {writes} записей, {checks} check"


# ── CLI ──────────────────────────────────────────────────────────────────────


def load(path: Path) -> bytes:
    suffix = path.suffix.lower()
    if suffix == ".c":
        return parse_c_array(path.read_text(encoding="utf-8"))
    if suffix == ".bin":
        return path.read_bytes()
    if suffix in (".txt", ".dcd"):
        return encode(parse_txt(path.read_text(encoding="utf-8")))
    raise DcdError(f"неизвестный тип входа '{suffix}' (ожидается .c/.bin/.txt)")


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input", type=Path, help=".c (Config Tools), .bin или .txt")
    ap.add_argument("--bin", type=Path, help="записать сырой DCD (для nxpimage hab)")
    ap.add_argument("--c", type=Path, help="записать C-массив для bsp_sdram_run_dcd()")
    ap.add_argument("--symbol", default="g_sdram_dcd", help="имя C-массива (default: g_sdram_dcd)")
    ap.add_argument("--txt", type=Path, help="записать текстовый листинг")
    ap.add_argument("--compare", type=Path, help="сверить результат побайтно с этим .bin")
    ap.add_argument("--werror", action="store_true", help="предупреждения считать ошибками")
    args = ap.parse_args(argv)

    try:
        data = load(args.input)
        cmds = parse_bin(data)
        data = data[: struct.unpack(">H", data[1:3])[0]]
    except (DcdError, OSError) as exc:
        print(f"dcd_tool: ошибка: {exc}", file=sys.stderr)
        return 1

    errors = pfd_frac_errors(cmds)
    for error in errors:
        print(f"dcd_tool: ошибка: {error}", file=sys.stderr)
    if errors:
        return 1

    warnings = rom_address_warnings(cmds)
    for warning in warnings:
        print(f"dcd_tool: предупреждение: {warning}", file=sys.stderr)

    if args.bin:
        args.bin.parent.mkdir(parents=True, exist_ok=True)
        args.bin.write_bytes(data)
    if args.c:
        args.c.parent.mkdir(parents=True, exist_ok=True)
        args.c.write_text(to_c(cmds, data, args.symbol, args.input.name), encoding="utf-8")
    if args.txt:
        args.txt.parent.mkdir(parents=True, exist_ok=True)
        args.txt.write_text(to_txt(cmds), encoding="utf-8")

    print(f"dcd_tool: {args.input.name}: {summary(cmds, data)}")

    if args.compare:
        ref = args.compare.read_bytes()
        if ref != data:
            diff_at = next((i for i, (a, b) in enumerate(zip(ref, data)) if a != b), None)
            where = f"первое отличие @{diff_at}" if diff_at is not None else "разная длина"
            print(
                f"dcd_tool: НЕ совпадает с {args.compare} ({len(ref)} vs {len(data)} байт, {where})",
                file=sys.stderr,
            )
            return 2
        print(f"dcd_tool: совпадает с {args.compare} побайтно")

    return 1 if (warnings and args.werror) else 0


if __name__ == "__main__":
    sys.exit(main())
