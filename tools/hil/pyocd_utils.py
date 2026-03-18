"""
pyocd_utils.py — переиспользуемые утилиты для работы с RT1052 через pyOCD.

Используется двумя способами:
  1. conftest.py         — импортирует flexram_init / load_elf / run_from_vectors
  2. load_and_run.py     — вызывает load_and_run() из CLI

Не содержит ничего pytest-специфичного — чистый Python.
"""

from __future__ import annotations

import logging
from contextlib import contextmanager
from typing import Generator

from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# FLEXRAM
# ---------------------------------------------------------------------------
_GPR16 = 0x400AC040  # IOMUXC_GPR16: бит 2 = FLEXRAM_BANK_CFG_SEL
_GPR17 = 0x400AC044  # IOMUXC_GPR17: конфигурация банков
#   0xFFFFAA55 → 128KB ITCM (0b01) + 128KB DTCM (0b10) + 256KB OCRAM (0b11)
_FLEXRAM_CFG = 0xFFFFAA55


def flexram_init(target: Target) -> None:
    """Настроить FLEXRAM: 128KB ITCM + 128KB DTCM + 256KB OCRAM."""
    target.write32(_GPR16, target.read32(_GPR16) | (1 << 2))
    target.write32(_GPR17, _FLEXRAM_CFG)
    log.debug(
        "FLEXRAM: GPR16=0x%08X  GPR17=0x%08X",
        target.read32(_GPR16),
        target.read32(_GPR17),
    )


# ---------------------------------------------------------------------------
# ELF loader
# ---------------------------------------------------------------------------
def load_elf(target: Target, elf_path: str) -> None:
    """Записать все PT_LOAD сегменты ELF по физическим адресам."""
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        for seg in elf.iter_segments():
            if seg.header.p_type != "PT_LOAD":
                continue
            if seg.header.p_filesz == 0:
                continue
            addr = seg.header.p_paddr
            data = list(seg.data())
            target.write_memory_block8(addr, data)
            log.debug("segment → 0x%08X  %5d bytes", addr, len(data))
    log.info("Loaded: %s", elf_path)


# ---------------------------------------------------------------------------
# Запуск из таблицы векторов
# ---------------------------------------------------------------------------
def run_from_vectors(target: Target) -> int:
    """
    Взять SP и PC из таблицы векторов (ITCM 0x00000000),
    выставить регистры, resume.

    Returns:
        PC (адрес Reset_Handler) для проверки вызывающей стороной.
    """
    sp = target.read32(0x00000000)
    pc = target.read32(0x00000004)
    log.debug("Vector table → SP=0x%08X  PC=0x%08X", sp, pc)

    if not (0x20000000 <= sp <= 0x20040000):
        raise RuntimeError(f"SP вне DTCM: 0x{sp:08X} — ELF загружен корректно?")
    if not (0x00000400 <= pc <= 0x00020000):
        raise RuntimeError(f"PC вне ITCM: 0x{pc:08X} — FLEXRAM настроен?")

    target.write_core_register("sp", sp)
    target.write_core_register("pc", pc)
    target.resume()

    log.info("Running from PC=0x%08X", pc)
    return pc


# ---------------------------------------------------------------------------
# Контекстный менеджер: сессия pyOCD
# ---------------------------------------------------------------------------
@contextmanager
def open_target(
    frequency: int = 1_000_000,
) -> Generator[Target, None, None]:
    """
    Открыть pyOCD-сессию с первым найденным пробником.

    Использование:
        with open_target() as target:
            flexram_init(target)
            load_elf(target, "firmware.elf")
            run_from_vectors(target)
    """
    with ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        connect_mode="attach",
        frequency=frequency,
        options={"no_config": True},
    ) as session:
        target = session.target
        target.halt()
        log.debug("Halted. PC = 0x%08X", target.read_core_register("pc"))
        yield target


# ---------------------------------------------------------------------------
# Комбо: всё за один вызов
# ---------------------------------------------------------------------------
def load_and_run(elf_path: str, frequency: int = 1_000_000) -> None:
    """
    Полный цикл: подключиться → FLEXRAM → загрузить ELF → запустить.
    Удобно для CLI и одиночных скриптов.
    """
    with open_target(frequency) as target:
        flexram_init(target)
        load_elf(target, elf_path)
        run_from_vectors(target)