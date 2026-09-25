"""Тесты tools/host/dcd_tool.py.

Запуск (devcontainer): just build::test-tools
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest

HOST_DIR = Path(__file__).resolve().parents[1]
REPO = HOST_DIR.parents[1]
sys.path.insert(0, str(HOST_DIR))

import dcd_tool as dt  # noqa: E402

LEGACY_BIN = HOST_DIR / "dcd" / "dcd.bin"
BOARD_DCD_C = REPO / "bsp" / "generated" / "dcd" / "board" / "dcd.c"
EVK_DCD_C = REPO / "sdk" / "boards" / "evkbimxrt1050" / "dcd.c"

QOS_ADDRS = [0x41044100, 0x41044104, 0x41442100, 0x41442104]


@pytest.fixture(scope="module")
def legacy() -> bytes:
    return LEGACY_BIN.read_bytes()


def _hdr(body: bytes) -> bytes:
    return struct.pack(">BHB", 0xD2, 4 + len(body), 0x41) + body


# ── Эталонные DCD ────────────────────────────────────────────────────────────


def test_legacy_bin_parses_with_known_shape(legacy):
    cmds = dt.parse_bin(legacy)
    writes = sum(len(c.pairs) for c in cmds if isinstance(c, dt.Write))
    checks = sum(isinstance(c, dt.Check) for c in cmds)
    # те же числа, что проверяет tests/host/dcd_exec (интерпретатор на таргете)
    assert (len(legacy), len(cmds), writes, checks) == (1208, 9, 142, 4)


def test_legacy_bin_roundtrip_bin_and_txt(legacy):
    cmds = dt.parse_bin(legacy)
    assert dt.encode(cmds) == legacy
    assert dt.encode(dt.parse_txt(dt.to_txt(cmds))) == legacy


def test_legacy_only_qos_writes_are_outside_rom_table(legacy):
    warnings = dt.rom_address_warnings(dt.parse_bin(legacy))
    assert [int(w.split()[2], 16) for w in warnings] == QOS_ADDRS


@pytest.mark.skipif(not BOARD_DCD_C.exists(), reason="нет bsp/generated/dcd/board/dcd.c")
def test_config_tools_dcd_equals_legacy_without_qos(legacy):
    """TFT_DCD.mex = импорт legacy dcd.bin минус последняя команда (NIC-301 QoS)."""
    data = dt.parse_c_array(BOARD_DCD_C.read_text(encoding="utf-8"))
    cmds = dt.parse_bin(data)
    legacy_cmds = dt.parse_bin(legacy)
    assert cmds == legacy_cmds[:-1]
    assert [a for a, _ in legacy_cmds[-1].pairs] == QOS_ADDRS
    assert dt.rom_address_warnings(cmds) == []


def test_evk_config_tools_dcd_parses():
    """dcd.c от EVK — тоже вывод Config Tools (DCDx v2.0); парсер C-массива его понимает."""
    data = dt.parse_c_array(EVK_DCD_C.read_text(encoding="utf-8"))
    cmds = dt.parse_bin(data)
    assert len(data) == 0x410
    assert dt.encode(cmds) == data
    # финальная запись EVK — SDRAMCR3 с REN=1
    assert cmds[-1] == dt.Write(4, 0, [(0x402F004C, 0x50210A09)])


# ── Семантика флагов (RM табл. 9-41 / 9-45, u-boot imximage.h) ───────────────


@pytest.mark.parametrize(
    ("par", "name"),
    [
        (0x04, "check_all_bits_clear"),
        (0x0C, "check_any_bit_clear"),
        (0x14, "check_all_bits_set"),
        (0x1C, "check_any_bit_set"),
    ],
)
def test_check_names_follow_rm(par, name):
    data = _hdr(struct.pack(">BHBII", 0xCF, 12, par, 0x402F003C, 1))
    assert dt.to_txt(dt.parse_bin(data)).split()[0] == name


@pytest.mark.parametrize(
    ("par", "name"),
    [
        (0x04, "write_value"),
        (0x0C, "write_clear_bits"),
        (0x14, "write_value_set"),
        (0x1C, "write_set_bits"),
    ],
)
def test_write_names_follow_rm(par, name):
    data = _hdr(struct.pack(">BHBII", 0xCC, 12, par, 0x402F0000, 1))
    txt = dt.to_txt(dt.parse_bin(data))
    assert txt.split()[0] == name
    assert dt.encode(dt.parse_txt(txt)) == data


# ── Текстовый формат ─────────────────────────────────────────────────────────


def test_txt_merges_consecutive_writes_and_splits_on_blank_line():
    cmds = dt.parse_txt(
        """
        # комментарий
        write_value 4 0x402F0000 0x1
        write_value 4 0x402F0004 0x2   # хвостовой комментарий
        write_set_bits 4 0x402F0008 0x4

        write_value 4 0x402F000C 0x8
        check_any_bit_set 4 0x402F003C 0x1 100
        nop
        """
    )
    assert cmds == [
        dt.Write(4, 0, [(0x402F0000, 1), (0x402F0004, 2)]),
        dt.Write(4, 3, [(0x402F0008, 4)]),
        dt.Write(4, 0, [(0x402F000C, 8)]),
        dt.Check(4, 3, 0x402F003C, 1, 100),
        dt.Nop(),
    ]


def test_txt_errors_are_reported_with_line():
    with pytest.raises(dt.DcdError, match="строка 2"):
        dt.parse_txt("nop\nwrite_value 4 0x402F0002 0x1\n")
    with pytest.raises(dt.DcdError, match="неизвестная команда"):
        dt.parse_txt("unlock 4 0 0\n")


# ── Валидация (те же правила, что dcd_exec_validate) ─────────────────────────


@pytest.mark.parametrize(
    "data",
    [
        b"\xd2\x00\x04\x40",  # version
        b"\xd1\x00\x04\x41",  # tag
        b"\xd2\x00\x08\x41",  # длина больше буфера
        _hdr(struct.pack(">BHBII", 0xCC, 12, 0x04, 0x402F0002, 1)),  # выравнивание
        _hdr(struct.pack(">BHBII", 0xCC, 12, 0x02, 0x402F0000, 0x10000)),  # шире 2 байт
        _hdr(struct.pack(">BHBII", 0xCC, 12, 0x03, 0x402F0000, 1)),  # ширина 3
        _hdr(struct.pack(">BHB", 0xCC, 4, 0x04)),  # write без пар
        _hdr(struct.pack(">BHBIII", 0xCF, 20, 0x1C, 0x402F003C, 1, 0) + b"\0" * 4),  # check 20
        _hdr(struct.pack(">BHBI", 0xC0, 8, 0, 0)),  # NOP длиной 8
        _hdr(struct.pack(">BHB", 0xB2, 4, 0)),  # Unlock
    ],
)
def test_invalid_dcd_is_rejected(data):
    with pytest.raises(dt.DcdError):
        dt.parse_bin(data)


def test_encode_rejects_oversized_dcd():
    with pytest.raises(dt.DcdError, match="1768"):
        dt.encode([dt.Nop()] * 450)


# ── C-вывод и CLI ────────────────────────────────────────────────────────────


def test_generated_c_roundtrips(legacy):
    cmds = dt.parse_bin(legacy)
    source = dt.to_c(cmds, legacy, "g_sdram_dcd", "dcd.bin")
    assert "const uint8_t g_sdram_dcd[]" in source
    assert "const size_t g_sdram_dcd_size" in source
    assert dt.parse_c_array(source) == legacy


def test_cli_writes_outputs_and_compares(tmp_path, legacy):
    out_bin, out_c, out_txt = tmp_path / "d.bin", tmp_path / "d.c", tmp_path / "d.txt"
    rc = dt.main(
        [str(LEGACY_BIN), "--bin", str(out_bin), "--c", str(out_c), "--txt", str(out_txt),
         "--compare", str(LEGACY_BIN)]
    )
    assert rc == 0
    assert out_bin.read_bytes() == legacy
    assert dt.parse_c_array(out_c.read_text(encoding="utf-8")) == legacy
    assert dt.encode(dt.parse_txt(out_txt.read_text(encoding="utf-8"))) == legacy


def test_cli_werror_and_mismatch(tmp_path):
    assert dt.main([str(LEGACY_BIN), "--werror"]) == 1  # QoS вне RM табл. 9-42
    other = tmp_path / "other.bin"
    other.write_bytes(_hdr(b""))
    assert dt.main([str(LEGACY_BIN), "--compare", str(other)]) == 2


# ── Перекрёстная проверка с SPSDK (побайтно, без имён операций) ──────────────


def test_spsdk_agrees_on_binary_structure(legacy):
    seg_dcd = pytest.importorskip("spsdk.image.hab.segments.seg_dcd")
    for data in (legacy, dt.parse_c_array(EVK_DCD_C.read_text(encoding="utf-8"))):
        assert seg_dcd.SegDCD.parse(data).export() == data


# ── Варианты для hil_sdram_stress (tests/target/hil_sdram_stress/dcd) ───────

VARIANTS_DIR = REPO / "tests" / "target" / "hil_sdram_stress" / "dcd"
VARIANTS = sorted(p.name for p in VARIANTS_DIR.glob("*.txt"))

SEMC = 0x402F0000
MCR, INTR, CR2, CR3, IPCMD = SEMC, SEMC + 0x3C, SEMC + 0x48, SEMC + 0x4C, SEMC + 0x9C
BR = [SEMC + 0x10 + 4 * i for i in range(9)]
CBCDR, PFD_480, PFD_528 = 0x400FC014, 0x400D80F0, 0x400D8100
PFD_RESET = {PFD_480: 0x0F1A231B, PFD_528: 0x1018101B}
# SEMC-поля CBCDR (PODF, ALT_CLK_SEL, CLK_SEL): alt ← PLL2 PFD2 /2 или PLL3 PFD1 /5
SEMC_PLL2_PFD2_DIV2, SEMC_PLL3_PFD1_DIV5 = 0x00010040, 0x000400C0
MUX_EMC = [0x401F8014 + 4 * n for n in range(42)]


def simulate(cmds, regs):
    """Применить команды к словарю регистров (RM табл. 9-41); check считаем выполненным.

    Возвращает лог IP-команд SEMC: (код, был ли INTR сброшен перед ней).
    """
    ip_log, intr_cleared = [], False
    for cmd in cmds:
        if not isinstance(cmd, dt.Write):
            continue
        for addr, value in cmd.pairs:
            cur = regs.get(addr, 0)
            regs[addr] = {0: value, 2: value, 1: cur & ~value, 3: cur | value}[cmd.flags]
            if addr == INTR:
                intr_cleared = (value & 0x3) == 0x3
            if addr == IPCMD:
                ip_log.append((value & 0xFFFF, intr_cleared))
                intr_cleared = False
    return ip_log


def load_variant(name):
    return dt.parse_bin(dt.encode(dt.parse_txt((VARIANTS_DIR / name).read_text(encoding="utf-8"))))


def test_all_expected_variants_exist():
    assert set(VARIANTS) == {
        "legacy.txt", "sdram_c.txt", "candidate.txt", "candidate_no_refresh.txt",
        "candidate_dqsmd0.txt", "candidate_dqsmd0_132mhz.txt", "candidate_164mhz.txt",
        "candidate_dqsmd0_148mhz.txt", "candidate_dqsmd0_153mhz.txt", "candidate_dqsmd0_158mhz.txt",
        "candidate_dqsmd0_164mhz.txt",
    }


@pytest.mark.parametrize("name", VARIANTS)
def test_variant_is_rom_compatible(name):
    cmds = load_variant(name)
    assert len(dt.encode(cmds)) <= dt.MAX_SIZE
    assert dt.rom_address_warnings(cmds) == []


@pytest.mark.parametrize("name", VARIANTS)
def test_variant_has_no_pfd_frac_errors(name):
    assert dt.pfd_frac_errors(load_variant(name)) == []


@pytest.mark.parametrize("name", VARIANTS)
def test_variant_changes_only_semc_clock_fields(name):
    """В рантайме после BOARD_BootClockRUN() AHB/IPG трогать нельзя (legacy писал CBCDR целиком)."""
    runtime_cbcdr = 0x000A8300  # AHB_PODF=0, IPG_PODF=3 (clock_config.c), SEMC — сброс
    regs = {CBCDR: runtime_cbcdr, **PFD_RESET}
    simulate(load_variant(name), regs)
    assert regs[CBCDR] & ~0x000700C0 == runtime_cbcdr & ~0x000700C0
    semc = SEMC_PLL3_PFD1_DIV5 if name == "candidate_dqsmd0_132mhz.txt" else SEMC_PLL2_PFD2_DIV2
    assert regs[CBCDR] & 0x000700C0 == semc


def test_legacy_txt_equals_production_dcd_except_cbcdr():
    def flat(cmds, drop):
        out = []
        for cmd in cmds:
            if isinstance(cmd, dt.Write):
                out += [(cmd.flags, a, v) for a, v in cmd.pairs if a not in drop]
            elif isinstance(cmd, dt.Check) and cmd.addr not in drop:
                out.append(("check", cmd.flags, cmd.addr, cmd.mask, cmd.count))
        return out

    prod = dt.parse_bin(dt.parse_c_array(BOARD_DCD_C.read_text(encoding="utf-8")))
    legacy = load_variant("legacy.txt")
    assert flat(legacy, {CBCDR, 0x400FC048}) == flat(prod, {CBCDR})


@pytest.mark.parametrize(
    ("name", "dqsmd", "cr2", "cr3", "pfd", "refreshes"),
    [
        ("candidate.txt", 1, 0x0002090A, 0x501C0A09, (PFD_528, 2, 35), 8),
        ("candidate_no_refresh.txt", 1, 0x0002090A, 0x501C0A08, (PFD_528, 2, 35), 8),
        ("candidate_dqsmd0.txt", 0, 0x0002090A, 0x501C0A09, (PFD_528, 2, 35), 8),
        ("candidate_dqsmd0_132mhz.txt", 0, 0x0002090A, 0x501C0A09, (PFD_480, 1, 13), 8),
        ("candidate_164mhz.txt", 1, 0x00020A0C, 0x50210A09, (PFD_528, 2, 29), 8),
        ("candidate_dqsmd0_148mhz.txt", 0, 0x00020A0C, 0x50210A09, (PFD_528, 2, 32), 8),
        ("candidate_dqsmd0_153mhz.txt", 0, 0x00020A0C, 0x50210A09, (PFD_528, 2, 31), 8),
        ("candidate_dqsmd0_158mhz.txt", 0, 0x00020A0C, 0x50210A09, (PFD_528, 2, 30), 8),
        ("candidate_dqsmd0_164mhz.txt", 0, 0x00020A0C, 0x50210A09, (PFD_528, 2, 29), 8),
        ("sdram_c.txt", 1, 0x00020201, 0x50210A09, (PFD_528, 2, 35), 2),
    ],
)
def test_variant_semantics(name, dqsmd, cr2, cr3, pfd, refreshes):
    regs = {**PFD_RESET, MUX_EMC[40]: 0xAA, MUX_EMC[41]: 0xBB}
    ip_log = simulate(load_variant(name), regs)

    assert (regs[MCR] >> 2) & 1 == dqsmd
    assert regs[MUX_EMC[39]] == (0x10 if dqsmd else 0x05)  # SEMC_DQS+SION или GPIO3_IO25
    assert regs[CR2] == cr2
    assert regs[CR3] == cr3
    pfd_reg, n, frac = pfd
    assert (regs[pfd_reg] >> (8 * n)) & 0xBF == frac  # FRAC, CLKGATE=0
    for reg, reset in PFD_RESET.items():  # остальные PFD не тронуты (legacy обнулял PFD0/1/3)
        for k in range(4):
            if (reg, k) != (pfd_reg, n):
                assert (regs[reg] >> (8 * k)) & 0xBF == (reset >> (8 * k)) & 0xBF
    # init: precharge-all → N×auto-refresh → mode set; INTR сброшен перед каждой
    assert [c for c, _ in ip_log] == [0x000F] + [0x000C] * refreshes + [0x000A]
    assert all(cleared for _, cleared in ip_log)
    if name != "sdram_c.txt":
        assert (regs[MUX_EMC[40]], regs[MUX_EMC[41]]) == (0xAA, 0xBB)  # EMC_40/41 — pin_mux
        assert all(regs[br] & 1 == 0 for br in BR[1:])  # только BR0 валиден
        assert regs[BR[0]] == 0x8000001B


# ── Lint PFD FRAC (RM: 12…35) ────────────────────────────────────────────────


@pytest.mark.parametrize(
    ("body", "expected"),
    [
        # clear+set, как в вариантах: промежуточный FRAC=0 не ошибка
        ("write_clear_bits 4 0x400D8100 0x00BF0000\n\nwrite_set_bits 4 0x400D8100 0x00230000", []),
        ("write_clear_bits 4 0x400D8100 0x00BF0000\n\nwrite_set_bits 4 0x400D8100 0x00240000",
         ["PFD_528.PFD2_FRAC = 36"]),
        ("write_clear_bits 4 0x400D80F0 0x0000BF00\n\nwrite_set_bits 4 0x400D80F0 0x00000B00",
         ["PFD_480.PFD1_FRAC = 11"]),
        ("write_set_bits 4 0x400D8100 0x00800000", []),  # PFD2 закрыт CLKGATE — FRAC неважен
        ("write_value 4 0x400D8100 0x00230000", []),  # legacy: PFD0/1/3=0 — известно, не ловим
        ("write_value 4 0x400D8100 0x0A23000C", ["PFD_528.PFD3_FRAC = 10"]),
        ("write_value 4 0x402F0000 0x00240000", []),  # не PFD
    ],
)
def test_pfd_frac_lint(body, expected):
    errors = dt.pfd_frac_errors(dt.parse_txt(body))
    assert [e.split(" вне ")[0] for e in errors] == expected


def test_cli_fails_on_pfd_frac_error(tmp_path):
    src = tmp_path / "bad.txt"
    src.write_text("write_set_bits 4 0x400D8100 0x003F0000\n", encoding="utf-8")
    out = tmp_path / "bad.c"
    assert dt.main([str(src), "--c", str(out)]) == 1
    assert not out.exists()
