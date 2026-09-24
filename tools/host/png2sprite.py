#!/usr/bin/env python3
"""
png2sprite — PNG → вкомпилированный в образ спрайт (`const tImage`) для tft_app.

Фаза 4.1. Требования к исходникам, раскладка экрана и поведение при отсутствии
спрайта — docs/tft_app/FALLBACK_SPRITES.md.

Зависимостей нет: PNG разбирается через stdlib `zlib`. Это осознанно — тулза
уезжает в CI, а формат исходников и так сужен до 8-битного неинтерлейсированного
PNG, разбор которого занимает полсотни строк. Заодно «отвергнуть неподходящий
файл» получается само, а не отдельной проверкой поверх чужой библиотеки.

ДВА ЦВЕТОВЫХ ПУТИ (см. gfx_blit.c — там же приёмная сторона):
  color (умолчание) — в поток идёт ARGB8888 как есть;
  tint              — в поток идёт 0xVVVVVVVV, где V = АЛЬФА исходника, а цвет
                      задаёт прошивка в рантайме. Годится монохромным спрайтам;
                      попутно вычищает мусорные RGB-значения внутри фигуры,
                      потому что RGB не читается вовсе.

ПОДАВЛЕНИЕ ШУМА (--denoise, по умолчанию 2). Эталонные PNG несут разброс ±1 в
плоских заливках — подпись lossy-этапа в цепочке экспорта. Для глаза это ничто,
но для RLE смертельно: прогоны рвутся каждые 1–2 пикселя и сжатие падает с ×11
до ×2.3. Снятие шума на ±2 даёт видимую ошибку не выше 5/255 при композите на
чёрный (наш фон) и возвращает треть объёма. --denoise 0 отключает.

САМОПРОВЕРКА. После кодирования поток декодируется обратно тем же алгоритмом,
что и в gfx_blit.c, и сверяется попиксельно с тем, что просили нарисовать.
Расхождение — ошибка с ненулевым кодом возврата. Ошибка в кодировщике иначе
проявилась бы кашей на экране, где её дороже всего искать.
"""

import argparse
import os
import struct
import sys
import zlib

# ── Разбор PNG ───────────────────────────────────────────────────────────────

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
COLOR_TYPE = {0: "grey", 2: "RGB", 3: "palette", 4: "grey+alpha", 6: "RGBA"}
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


class PngError(Exception):
    """Файл не годится как исходник спрайта (с объяснением, что именно не так)."""


def _iter_chunks(raw):
    if raw[:8] != PNG_MAGIC:
        raise PngError("это не PNG")
    i = 8
    while i + 8 <= len(raw):
        (length,) = struct.unpack(">I", raw[i : i + 4])
        yield raw[i + 4 : i + 8], raw[i + 8 : i + 8 + length]
        i += 12 + length


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(data, width, height, nch):
    """Снять построчные фильтры PNG (типы 0..4)."""
    stride = width * nch
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for row in range(height):
        if pos >= len(data):
            raise PngError(f"поток IDAT оборван на строке {row}")
        ftype = data[pos]
        pos += 1
        line = bytearray(data[pos : pos + stride])
        pos += stride
        if len(line) != stride:
            raise PngError(f"строка {row} короче объявленной ширины")

        if ftype == 1:
            for x in range(nch, stride):
                line[x] = (line[x] + line[x - nch]) & 0xFF
        elif ftype == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ftype == 3:
            for x in range(stride):
                left = line[x - nch] if x >= nch else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 0xFF
        elif ftype == 4:
            for x in range(stride):
                left = line[x - nch] if x >= nch else 0
                upleft = prev[x - nch] if x >= nch else 0
                line[x] = (line[x] + _paeth(left, prev[x], upleft)) & 0xFF
        elif ftype != 0:
            raise PngError(f"неизвестный фильтр {ftype} в строке {row}")

        out += line
        prev = line
    return out


def decode_png(path):
    """→ (width, height, [(r, g, b, a), ...]). Кидает PngError на неподходящий файл."""
    raw = open(path, "rb").read()

    ihdr = idat = plte = trns = None
    for ctype, data in _iter_chunks(raw):
        if ctype == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", data)
        elif ctype == b"IDAT":
            idat = data if idat is None else idat + data
        elif ctype == b"PLTE":
            plte = data
        elif ctype == b"tRNS":
            trns = data

    if ihdr is None or idat is None:
        raise PngError("нет IHDR или IDAT")

    width, height, depth, color_type, _comp, _filt, interlace = ihdr

    # Формат сужен намеренно (docs/tft_app/FALLBACK_SPRITES.md §4): чем уже вход,
    # тем меньше поводов для «на моей машине выглядело иначе».
    if interlace:
        raise PngError("интерлейсинг (Adam7) не поддерживается — пересохраните без него")
    if depth != 8:
        raise PngError(f"{depth} бит на канал — нужен ровно 8")
    if color_type not in CHANNELS:
        raise PngError(f"неизвестный тип цвета {color_type}")
    if color_type == 3 and plte is None:
        raise PngError("палитровый PNG без PLTE")

    nch = CHANNELS[color_type]
    flat = _unfilter(zlib.decompress(idat), width, height, nch)

    pixels = []
    for i in range(width * height):
        chunk = flat[i * nch : (i + 1) * nch]
        if color_type == 6:
            pixels.append((chunk[0], chunk[1], chunk[2], chunk[3]))
        elif color_type == 2:
            pixels.append((chunk[0], chunk[1], chunk[2], 255))
        elif color_type == 0:
            pixels.append((chunk[0], chunk[0], chunk[0], 255))
        elif color_type == 4:
            pixels.append((chunk[0], chunk[0], chunk[0], chunk[1]))
        else:
            idx = chunk[0]
            r, g, b = plte[idx * 3 : idx * 3 + 3]
            a = trns[idx] if trns is not None and idx < len(trns) else 255
            pixels.append((r, g, b, a))

    return width, height, pixels, COLOR_TYPE[color_type]


# ── Подготовка пикселей ──────────────────────────────────────────────────────


def to_words(pixels, tint, denoise):
    """Пиксели → поток uint32 в том виде, в каком его прочтёт gfx_blit.c.

    Подавление шума — ПРОГОН-ОРИЕНТИРОВАННОЕ: пиксель, отличающийся от
    предыдущего не более чем на @p denoise в каждом канале, заменяется
    предыдущим. Это ровно то, что удлиняет прогоны RLE, и ровно то, что
    незаметно глазу. Порядок обхода — растровый, тот же, что у кодировщика.
    """
    alpha_tol = denoise + 1 if denoise else 0
    words = []
    prev = None
    for r, g, b, a in pixels:
        if alpha_tol:
            if a <= alpha_tol:
                # Полностью прозрачный пиксель обнуляется ЦЕЛИКОМ: тогда все
                # прозрачные пиксели одинаковы и сливаются в один прогон,
                # который прошивка пропускает не читая.
                r = g = b = a = 0
            elif a >= 255 - alpha_tol:
                a = 255
        if prev is not None and denoise:
            pr, pg, pb, pa = prev
            if (
                abs(r - pr) <= denoise
                and abs(g - pg) <= denoise
                and abs(b - pb) <= denoise
                and abs(a - pa) <= denoise
            ):
                r, g, b, a = prev
        prev = (r, g, b, a)
        # tint: прошивка читает покрытие из МЛАДШЕГО байта, поэтому альфа
        # размножается по всем четырём — так же запечены шрифты.
        words.append((a << 24) | (a << 16) | (a << 8) | a if tint else (a << 24) | (r << 16) | (g << 8) | b)
    return words


# ── RLE ──────────────────────────────────────────────────────────────────────

UNIQUE_MASK = 0xFFFFFF00
MAX_UNIQUE = 256
MAX_REPEAT = 0xFFFF


def rle_encode(words):
    """Кодировать поток. Формат — см. шапку gfx_blit.c.

    Прогон длиной ≥ 2 идёт REPEATABLE (2 слова на любую длину), одиночки
    копятся в UNIQUE-блоки по 256. Порог именно 2: прогон из двух в
    REPEATABLE стоит те же 2 слова, что и в UNIQUE, но при этом остаётся
    прогоном — а прозрачный прогон прошивка пропускает целиком, не читая и не
    записывая ни пикселя.
    """
    out = []
    pending = []

    def flush():
        while pending:
            take = pending[:MAX_UNIQUE]
            del pending[:MAX_UNIQUE]
            out.append(UNIQUE_MASK | ((0x100 - len(take)) & 0xFF))
            out.extend(take)

    i = 0
    n = len(words)
    while i < n:
        j = i
        while j + 1 < n and words[j + 1] == words[i] and (j - i + 1) < MAX_REPEAT:
            j += 1
        run = j - i + 1
        if run >= 2:
            flush()
            out.append(run)
            out.append(words[i])
        else:
            pending.append(words[i])
        i += run
    flush()
    return out


def rle_decode(stream, total):
    """Эталонный декодер — ЗЕРКАЛО blit_rle() из gfx_blit.c.

    Существует только ради самопроверки: если он и кодировщик разойдутся,
    расхождение поймается здесь, а не кашей на панели.
    """
    out = []
    idx = 0
    while len(out) < total:
        header = stream[idx]
        idx += 1
        if (header & UNIQUE_MASK) == UNIQUE_MASK:
            length = 0x100 - (header & 0xFF)
            for _ in range(length):
                if len(out) >= total:
                    break
                out.append(stream[idx])
                idx += 1
        else:
            length = header & 0xFFFF
            pixel = stream[idx]
            idx += 1
            out.extend([pixel] * min(length, total - len(out)))
    return out


# ── Генерация .c ─────────────────────────────────────────────────────────────

HEADER = """/*
 * СГЕНЕРИРОВАНО tools/host/png2sprite.py — РУКАМИ НЕ ПРАВИТЬ.
 * Источник: assets/fallback_sprites/{src}
 * {w}x{h}, путь цвета: {mode}, подавление шума: ±{denoise}
 * Сжатие RLE: {raw_kb:.1f} КБ → {rle_kb:.1f} КБ (x{ratio:.1f})
 */

#include "services/gfx.h"

"""


def emit_c(name, src, width, height, stream, mode, denoise, raw_bytes):
    rle_bytes = len(stream) * 4
    text = HEADER.format(
        src=src,
        w=width,
        h=height,
        mode=mode,
        denoise=denoise,
        raw_kb=raw_bytes / 1024,
        rle_kb=rle_bytes / 1024,
        ratio=raw_bytes / max(rle_bytes, 1),
    )
    text += f"static const uint32_t image_data_{name}[{len(stream)}] = {{\n"
    for i in range(0, len(stream), 8):
        row = ", ".join(f"0x{v:08X}" for v in stream[i : i + 8])
        text += f"    {row},\n"
    text += "};\n\n"
    text += f"const tImage {name} = {{ image_data_{name}, {width}, {height}, 32 }};\n"
    return text


# ── main ─────────────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description="PNG → const tImage для tft_app")
    ap.add_argument("sources", nargs="+", help="PNG-файлы или каталог с ними")
    ap.add_argument("-o", "--outdir", required=True, help="куда класть .c")
    ap.add_argument(
        "--tint",
        default="",
        help="список имён через запятую, которые паковать в tint-режиме "
        "(значима только альфа, цвет задаёт прошивка)",
    )
    ap.add_argument(
        "--denoise",
        type=int,
        default=2,
        help="допуск подавления шума на канал (0 — отключить, по умолчанию 2)",
    )
    args = ap.parse_args()

    paths = []
    for src in args.sources:
        if os.path.isdir(src):
            paths += [
                os.path.join(src, f) for f in sorted(os.listdir(src)) if f.endswith(".png")
            ]
        else:
            paths.append(src)

    tinted = {t.strip() for t in args.tint.split(",") if t.strip()}
    os.makedirs(args.outdir, exist_ok=True)

    total_raw = total_rle = 0
    failures = 0

    print(f"{'спрайт':<24} {'размер':>9} {'путь':>6} {'raw':>9} {'rle':>9} {'сжатие':>7}")
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            width, height, pixels, ctype = decode_png(path)
        except PngError as exc:
            print(f"{name:<24} ❌ отвергнут: {exc}", file=sys.stderr)
            failures += 1
            continue

        tint = name in tinted
        words = to_words(pixels, tint, args.denoise)
        stream = rle_encode(words)

        # Самопроверка: декодируем ЗЕРКАЛОМ прошивочного алгоритма.
        if rle_decode(stream, len(words)) != words:
            print(f"{name:<24} ❌ САМОПРОВЕРКА НЕ ПРОШЛА (ошибка кодировщика)", file=sys.stderr)
            failures += 1
            continue

        raw_bytes = len(words) * 4
        rle_bytes = len(stream) * 4
        total_raw += raw_bytes
        total_rle += rle_bytes

        out_path = os.path.join(args.outdir, f"{name}.c")
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(
                emit_c(name, os.path.basename(path), width, height, stream,
                       "tint" if tint else "color", args.denoise, raw_bytes)
            )

        print(
            f"{name:<24} {width:>4}x{height:<4} {'tint' if tint else 'color':>6} "
            f"{raw_bytes/1024:>7.1f}KB {rle_bytes/1024:>7.1f}KB {raw_bytes/max(rle_bytes,1):>6.1f}x"
        )

    if total_rle:
        print(
            f"\n  итого: {total_raw/1024:.1f} КБ → {total_rle/1024:.1f} КБ "
            f"(x{total_raw/total_rle:.1f}), файлов: {len(paths) - failures}"
        )
    if failures:
        print(f"  ❌ отвергнуто/провалено: {failures}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
