/*
 * test_dcd_exec.c — host-тест интерпретатора DCD (bsp/sdram/src/dcd_exec.c)
 *
 * Категория A: весь доступ к регистрам и времени идёт через dcd_exec_io_t,
 * здесь он подменён фейковой картой регистров и программируемыми часами.
 *
 * Эталон семантики — i.MX RT1050 RM Rev.4 §9.7.2 (табл. 9-41, 9-45).
 * Последний тест гоняет реальный legacy tools/host/dcd/dcd.bin.
 */

#include "unity.h"

#include "dcd_exec.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ── Фейковая карта регистров ──────────────────────────────────────────── */

#define FAKE_REGS_MAX 256U
#define FAKE_LOG_MAX  256U

typedef struct fake_reg_s
{
    uint32_t addr;
    uint32_t value;
} fake_reg_t;

typedef struct fake_write_s
{
    uint32_t addr;
    uint32_t value;
    uint8_t width;
} fake_write_t;

static fake_reg_t s_regs[FAKE_REGS_MAX];
static size_t s_reg_count;
static fake_write_t s_log[FAKE_LOG_MAX];
static size_t s_log_count;
static uint32_t s_reads;
static uint32_t s_now_ms;
static uint32_t s_ms_per_now_call; /* сколько «проходит» мс за один вызов now_ms() */
static uint32_t s_set_after_reads; /* через N чтений выставить s_set_mask в s_set_addr */
static uint32_t s_set_addr;
static uint32_t s_set_mask;

static fake_reg_t *reg_find(uint32_t addr)
{
    for (size_t i = 0U; i < s_reg_count; i++)
    {
        if (s_regs[i].addr == addr)
        {
            return &s_regs[i];
        }
    }
    return NULL;
}

static void reg_set(uint32_t addr, uint32_t value)
{
    fake_reg_t *p_reg = reg_find(addr);
    if (p_reg == NULL)
    {
        TEST_ASSERT_LESS_THAN(FAKE_REGS_MAX, s_reg_count);
        p_reg       = &s_regs[s_reg_count++];
        p_reg->addr = addr;
    }
    p_reg->value = value;
}

static uint32_t reg_get(uint32_t addr)
{
    const fake_reg_t *p_reg = reg_find(addr);
    return (p_reg != NULL) ? p_reg->value : 0U;
}

static uint32_t fake_read(uint32_t addr, uint8_t width)
{
    (void) width;
    s_reads++;
    if ((s_set_after_reads != 0U) && (s_reads >= s_set_after_reads))
    {
        reg_set(s_set_addr, reg_get(s_set_addr) | s_set_mask);
    }
    return reg_get(addr);
}

static void fake_write(uint32_t addr, uint32_t value, uint8_t width)
{
    TEST_ASSERT_LESS_THAN(FAKE_LOG_MAX, s_log_count);
    s_log[s_log_count++] = (fake_write_t) {.addr = addr, .value = value, .width = width};
    reg_set(addr, value);
}

static uint32_t fake_now_ms(void)
{
    const uint32_t NOW = s_now_ms;
    s_now_ms += s_ms_per_now_call;
    return NOW;
}

static const dcd_exec_io_t IO = {
    .read             = fake_read,
    .write            = fake_write,
    .now_ms           = fake_now_ms,
    .check_timeout_ms = 10U,
};

/* ── Сборка DCD в тесте ────────────────────────────────────────────────── */

static uint8_t s_dcd[DCD_EXEC_MAX_SIZE + 16U];
static size_t s_dcd_len;

static void put8(uint8_t v) { s_dcd[s_dcd_len++] = v; }

static void put16(uint16_t v)
{
    put8((uint8_t) (v >> 8));
    put8((uint8_t) v);
}

static void put32(uint32_t v)
{
    put16((uint16_t) (v >> 16));
    put16((uint16_t) v);
}

static void dcd_begin(void)
{
    s_dcd_len = 0U;
    put8(0xD2);
    put16(0U); /* длина — в dcd_end() */
    put8(0x41);
}

static size_t dcd_end(void)
{
    s_dcd[1] = (uint8_t) (s_dcd_len >> 8);
    s_dcd[2] = (uint8_t) s_dcd_len;
    return s_dcd_len;
}

/** Write data с одной парой. flags: bit0 — Mask, bit1 — Set (как в RM). */
static void cmd_write1(uint8_t width, uint8_t flags, uint32_t addr, uint32_t value)
{
    put8(0xCC);
    put16(12U);
    put8((uint8_t) ((flags << 3) | width));
    put32(addr);
    put32(value);
}

static void cmd_check(uint8_t width, uint8_t flags, uint32_t addr, uint32_t mask, bool has_count,
                      uint32_t count)
{
    put8(0xCF);
    put16(has_count ? 16U : 12U);
    put8((uint8_t) ((flags << 3) | width));
    put32(addr);
    put32(mask);
    if (has_count)
    {
        put32(count);
    }
}

static void cmd_nop(void)
{
    put8(0xC0);
    put16(4U);
    put8(0U);
}

#define FLAGS_WRITE_VALUE  0U /* Mask=0 Set=0 */
#define FLAGS_WRITE_VALUE2 2U /* Mask=0 Set=1 — тоже запись значения */
#define FLAGS_CLEAR_BITS   1U /* Mask=1 Set=0 */
#define FLAGS_SET_BITS     3U /* Mask=1 Set=1 */

#define FLAGS_ALL_CLEAR 0U
#define FLAGS_ALL_SET   2U
#define FLAGS_ANY_CLEAR 1U
#define FLAGS_ANY_SET   3U

#define REG_A 0x402F0000UL
#define REG_B 0x402F0004UL

void setUp(void)
{
    memset(s_regs, 0, sizeof(s_regs));
    memset(s_log, 0, sizeof(s_log));
    s_reg_count       = 0U;
    s_log_count       = 0U;
    s_reads           = 0U;
    s_now_ms          = 0U;
    s_ms_per_now_call = 0U;
    s_set_after_reads = 0U;
    s_dcd_len         = 0U;
}

void tearDown(void) {}

/* ── Аргументы и заголовок ─────────────────────────────────────────────── */

void test_null_args_return_param(void)
{
    dcd_begin();
    const size_t LEN = dcd_end();

    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, dcd_exec_run(NULL, LEN, &IO, NULL));
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, dcd_exec_run(s_dcd, LEN, NULL, NULL));

    dcd_exec_io_t io_no_write = IO;
    io_no_write.write         = NULL;
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, dcd_exec_run(s_dcd, LEN, &io_no_write, NULL));
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, dcd_exec_validate(NULL, LEN, NULL));
}

void test_header_only_is_valid_and_does_nothing(void)
{
    dcd_begin();
    dcd_exec_result_t res;

    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, &res));
    TEST_ASSERT_EQUAL_UINT32(0U, res.commands);
    TEST_ASSERT_EQUAL_size_t(4U, res.offset);
    TEST_ASSERT_EQUAL_size_t(0U, s_log_count);
}

void test_bad_header_is_invalid(void)
{
    dcd_begin();
    cmd_write1(4U, FLAGS_WRITE_VALUE, REG_A, 1U);
    const size_t LEN = dcd_end();

    s_dcd[0] = 0xD1; /* tag */
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_run(s_dcd, LEN, &IO, NULL));
    s_dcd[0] = 0xD2;

    s_dcd[3] = 0x40; /* version */
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_run(s_dcd, LEN, &IO, NULL));
    s_dcd[3] = 0x41;

    /* длина из заголовка больше буфера */
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_run(s_dcd, LEN - 1U, &IO, NULL));
    /* буфер короче заголовка */
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_run(s_dcd, 3U, &IO, NULL));

    TEST_ASSERT_EQUAL_size_t(0U, s_log_count);
}

void test_length_above_rom_limit_is_invalid(void)
{
    dcd_begin();
    while (s_dcd_len < (DCD_EXEC_MAX_SIZE + 4U))
    {
        cmd_nop();
    }
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));
}

void test_buffer_longer_than_header_length_is_ok(void)
{
    dcd_begin();
    cmd_write1(4U, FLAGS_WRITE_VALUE, REG_A, 7U);
    const size_t LEN = dcd_end();

    /* хвост буфера за длиной из заголовка не читается */
    s_dcd[LEN] = 0xFF;
    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, LEN + 8U, &IO, NULL));
    TEST_ASSERT_EQUAL_size_t(1U, s_log_count);
}

/* ── Write data (RM табл. 9-41) ────────────────────────────────────────── */

void test_write_value_multiple_pairs_in_order(void)
{
    dcd_begin();
    put8(0xCC);
    put16(4U + (3U * 8U));
    put8(0x04);
    put32(REG_A);
    put32(0x11111111UL);
    put32(REG_B);
    put32(0x22222222UL);
    put32(REG_A);
    put32(0x33333333UL);

    dcd_exec_result_t res;
    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, &res));
    TEST_ASSERT_EQUAL_size_t(3U, s_log_count);
    TEST_ASSERT_EQUAL_UINT32(REG_A, s_log[0].addr);
    TEST_ASSERT_EQUAL_HEX32(0x11111111UL, s_log[0].value);
    TEST_ASSERT_EQUAL_UINT32(REG_B, s_log[1].addr);
    TEST_ASSERT_EQUAL_HEX32(0x33333333UL, reg_get(REG_A));
    TEST_ASSERT_EQUAL_UINT32(3U, res.writes);
    TEST_ASSERT_EQUAL_UINT32(1U, res.commands);
}

void test_write_flags_follow_rm_table(void)
{
    reg_set(REG_A, 0xF0F0F0F0UL);
    reg_set(REG_B, 0xF0F0F0F0UL);
    dcd_begin();
    cmd_write1(4U, FLAGS_CLEAR_BITS, REG_A, 0x30000030UL);
    cmd_write1(4U, FLAGS_SET_BITS, REG_B, 0x0000000FUL);
    cmd_write1(4U, FLAGS_WRITE_VALUE2, 0x402F0008UL, 0xABCDUL);

    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, NULL));
    TEST_ASSERT_EQUAL_HEX32(0xC0F0F0C0UL, reg_get(REG_A)); /* &= ~mask */
    TEST_ASSERT_EQUAL_HEX32(0xF0F0F0FFUL, reg_get(REG_B)); /* |= mask  */
    TEST_ASSERT_EQUAL_HEX32(0xABCDUL, reg_get(0x402F0008UL)); /* Mask=0 Set=1 — запись */
}

void test_write_widths_1_and_2_are_passed_through(void)
{
    dcd_begin();
    cmd_write1(1U, FLAGS_WRITE_VALUE, 0x402F0011UL, 0xABU);
    cmd_write1(2U, FLAGS_WRITE_VALUE, 0x402F0012UL, 0xBEEFU);

    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, NULL));
    TEST_ASSERT_EQUAL_UINT8(1U, s_log[0].width);
    TEST_ASSERT_EQUAL_UINT8(2U, s_log[1].width);
    TEST_ASSERT_EQUAL_HEX32(0xBEEFU, s_log[1].value);
}

void test_invalid_write_rejects_whole_dcd_before_any_write(void)
{
    /* Первая команда корректна, вторая — невыровненный адрес. Ни одной записи. */
    dcd_begin();
    cmd_write1(4U, FLAGS_WRITE_VALUE, REG_A, 1U);
    cmd_write1(4U, FLAGS_WRITE_VALUE, 0x402F0002UL, 1U);

    dcd_exec_result_t res;
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_run(s_dcd, dcd_end(), &IO, &res));
    TEST_ASSERT_EQUAL_size_t(0U, s_log_count);
    TEST_ASSERT_EQUAL_size_t(16U, res.offset); /* 4 (заголовок) + 12 (первая команда) */
}

void test_value_wider_than_width_is_invalid(void)
{
    dcd_begin();
    cmd_write1(2U, FLAGS_WRITE_VALUE, 0x402F0010UL, 0x10000UL);
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));

    dcd_begin();
    cmd_write1(1U, FLAGS_WRITE_VALUE, 0x402F0010UL, 0x100UL);
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));
}

void test_malformed_write_commands_are_invalid(void)
{
    /* ширина 3 */
    dcd_begin();
    cmd_write1(3U, FLAGS_WRITE_VALUE, REG_A, 1U);
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));

    /* без пар */
    dcd_begin();
    put8(0xCC);
    put16(4U);
    put8(0x04);
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));

    /* неполная пара */
    dcd_begin();
    put8(0xCC);
    put16(8U);
    put8(0x04);
    put32(REG_A);
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));
}

void test_command_crossing_dcd_end_is_invalid(void)
{
    dcd_begin();
    cmd_write1(4U, FLAGS_WRITE_VALUE, REG_A, 1U);
    const size_t LEN = dcd_end();
    s_dcd[6] = 20U; /* длина команды 20 > оставшихся 12 */
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, LEN, NULL));

    /* хвост короче заголовка команды */
    dcd_begin();
    cmd_nop();
    put8(0xC0);
    put8(0x00);
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));
}

/* ── Check data (RM табл. 9-45) ────────────────────────────────────────── */

void test_check_conditions_follow_rm_table(void)
{
    reg_set(REG_A, 0x0000000CUL);
    dcd_begin();
    cmd_check(4U, FLAGS_ALL_CLEAR, REG_A, 0x00000003UL, true, 1U); /* 0x0C & 0x03 == 0      */
    cmd_check(4U, FLAGS_ALL_SET, REG_A, 0x0000000CUL, true, 1U);   /* & == mask             */
    cmd_check(4U, FLAGS_ANY_CLEAR, REG_A, 0x0000000EUL, true, 1U); /* 0x0C != 0x0E          */
    cmd_check(4U, FLAGS_ANY_SET, REG_A, 0x00000006UL, true, 1U);   /* 0x04 != 0             */

    dcd_exec_result_t res;
    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, &res));
    TEST_ASSERT_EQUAL_UINT32(4U, res.commands);
    TEST_ASSERT_EQUAL_UINT32(4U, s_reads);
}

void test_check_failing_condition_each_flavour(void)
{
    const uint8_t FLAVOURS[] = {FLAGS_ALL_CLEAR, FLAGS_ALL_SET, FLAGS_ANY_CLEAR, FLAGS_ANY_SET};
    const uint32_t VALUES[]  = {0x1U, 0x1U, 0x3U, 0x0U};

    for (size_t i = 0U; i < sizeof(FLAVOURS); i++)
    {
        setUp();
        reg_set(REG_A, VALUES[i]);
        dcd_begin();
        cmd_check(4U, FLAVOURS[i], REG_A, 0x3U, true, 1U);
        TEST_ASSERT_EQUAL_MESSAGE(BSP_ERR_TIMEOUT, dcd_exec_run(s_dcd, dcd_end(), &IO, NULL),
                                  "flavour index");
    }
}

void test_check_polls_until_condition(void)
{
    s_set_after_reads = 5U; /* бит появится на 5-м чтении */
    s_set_addr        = 0x402F003CUL;
    s_set_mask        = 0x1U;
    dcd_begin();
    cmd_check(4U, FLAGS_ANY_SET, 0x402F003CUL, 0x1U, false, 0U);

    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, NULL));
    TEST_ASSERT_EQUAL_UINT32(5U, s_reads);
}

void test_check_count_exhausted_stops_execution(void)
{
    dcd_begin();
    cmd_write1(4U, FLAGS_WRITE_VALUE, REG_A, 1U);
    cmd_check(4U, FLAGS_ANY_SET, REG_B, 0x1U, true, 3U);
    cmd_write1(4U, FLAGS_WRITE_VALUE, REG_A, 2U);

    dcd_exec_result_t res;
    TEST_ASSERT_EQUAL(BSP_ERR_TIMEOUT, dcd_exec_run(s_dcd, dcd_end(), &IO, &res));
    TEST_ASSERT_EQUAL_UINT32(3U, s_reads);
    TEST_ASSERT_EQUAL_size_t(1U, s_log_count); /* команда после check не исполнена */
    TEST_ASSERT_EQUAL_size_t(16U, res.offset);
    TEST_ASSERT_EQUAL_UINT32(1U, res.commands);
    TEST_ASSERT_EQUAL_UINT32(1U, res.writes);
}

void test_check_without_count_stops_on_timeout(void)
{
    s_ms_per_now_call = 1U;
    dcd_begin();
    cmd_check(4U, FLAGS_ANY_SET, REG_B, 0x1U, false, 0U);

    TEST_ASSERT_EQUAL(BSP_ERR_TIMEOUT, dcd_exec_run(s_dcd, dcd_end(), &IO, NULL));
    TEST_ASSERT_EQUAL_UINT32(IO.check_timeout_ms, s_reads);
}

void test_check_count_zero_is_nop(void)
{
    dcd_begin();
    cmd_check(4U, FLAGS_ANY_SET, REG_B, 0x1U, true, 0U);

    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, s_reads);
}

void test_malformed_check_is_invalid(void)
{
    dcd_begin();
    put8(0xCF);
    put16(20U);
    put8(0x1C);
    put32(REG_A);
    put32(1U);
    put32(1U);
    put32(0U);
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));

    dcd_begin();
    cmd_check(4U, FLAGS_ANY_SET, 0x402F0002UL, 0x1U, false, 0U); /* невыровненный */
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));
}

/* ── NOP и неизвестные команды ─────────────────────────────────────────── */

void test_nop_is_accepted(void)
{
    dcd_begin();
    cmd_nop();
    cmd_nop();
    dcd_exec_result_t res;

    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, &res));
    TEST_ASSERT_EQUAL_UINT32(2U, res.commands);
}

void test_nop_with_wrong_length_is_invalid(void)
{
    dcd_begin();
    put8(0xC0);
    put16(8U);
    put8(0U);
    put32(0U);
    TEST_ASSERT_EQUAL(BSP_ERR_INVALID, dcd_exec_validate(s_dcd, dcd_end(), NULL));
}

void test_unknown_command_is_not_supported(void)
{
    dcd_begin();
    cmd_write1(4U, FLAGS_WRITE_VALUE, REG_A, 1U);
    put8(0xB2); /* Unlock — в DCD не поддерживаем */
    put16(4U);
    put8(0U);

    TEST_ASSERT_EQUAL(BSP_ERR_NOT_SUPPORTED, dcd_exec_run(s_dcd, dcd_end(), &IO, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, s_log_count);
}

/* ── on_command ────────────────────────────────────────────────────────── */

static size_t s_cmd_offsets[8];
static size_t s_cmd_count;

static void record_command(size_t offset)
{
    if (s_cmd_count < 8U)
    {
        s_cmd_offsets[s_cmd_count] = offset;
    }
    s_cmd_count++;
}

void test_on_command_reports_each_element_offset_before_it_runs(void)
{
    s_cmd_count = 0U;
    dcd_exec_io_t io = IO;
    io.on_command    = record_command;
    dcd_begin();
    cmd_write1(4U, FLAGS_WRITE_VALUE, REG_A, 1U);        /* @4, пара @8 */
    cmd_nop();                                           /* @16 */
    cmd_check(4U, FLAGS_ANY_SET, REG_B, 0x1U, true, 1U); /* @20 — упадёт по count */

    TEST_ASSERT_EQUAL(BSP_ERR_TIMEOUT, dcd_exec_run(s_dcd, dcd_end(), &io, NULL));
    TEST_ASSERT_EQUAL_size_t(3U, s_cmd_count);
    TEST_ASSERT_EQUAL_size_t(8U, s_cmd_offsets[0]); /* write — смещение пары */
    TEST_ASSERT_EQUAL_size_t(16U, s_cmd_offsets[1]);
    TEST_ASSERT_EQUAL_size_t(20U, s_cmd_offsets[2]);
}

void test_on_command_null_is_allowed(void)
{
    dcd_begin();
    cmd_nop();
    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, dcd_end(), &IO, NULL));
}

/* ── Реальный legacy DCD ───────────────────────────────────────────────── */

/*
 * tools/host/dcd/dcd.bin: 9 команд, 142 записи, 4 check (SEMC_INTR.IPCMDDONE).
 * Эталонные значения — из разбора в docs/hardware/new_board_v3.2/SEMC_TIMING.md.
 */
void test_legacy_dcd_bin_executes_fully(void)
{
    FILE *p_file = fopen(LEGACY_DCD_PATH, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(p_file, LEGACY_DCD_PATH);
    const size_t SIZE = fread(s_dcd, 1U, sizeof(s_dcd), p_file);
    (void) fclose(p_file);

    reg_set(0x402F003CUL, 0x1U); /* SEMC_INTR.IPCMDDONE — IP-команды «завершаются» сразу */

    dcd_exec_result_t res;
    TEST_ASSERT_EQUAL(BSP_OK, dcd_exec_run(s_dcd, SIZE, &IO, &res));
    TEST_ASSERT_EQUAL_size_t(1208U, res.offset);
    TEST_ASSERT_EQUAL_UINT32(9U, res.commands);
    TEST_ASSERT_EQUAL_UINT32(142U, res.writes);

    TEST_ASSERT_EQUAL_HEX32(0x10000004UL, reg_get(0x402F0000UL)); /* SEMC_MCR       */
    TEST_ASSERT_EQUAL_HEX32(0x00020201UL, reg_get(0x402F0048UL)); /* SEMC_SDRAMCR2  */
    TEST_ASSERT_EQUAL_HEX32(0x08193D0FUL, reg_get(0x402F004CUL)); /* SEMC_SDRAMCR3  */
    TEST_ASSERT_EQUAL_HEX32(0x00230000UL, reg_get(0x400D8100UL)); /* PFD_528        */
    TEST_ASSERT_EQUAL_HEX32(0x00000033UL, reg_get(0x402F00A0UL)); /* IPTXDAT (MRS)  */
    TEST_ASSERT_EQUAL_HEX32(0x00000010UL, reg_get(0x401F80B0UL)); /* EMC_39: SION   */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_null_args_return_param);
    RUN_TEST(test_header_only_is_valid_and_does_nothing);
    RUN_TEST(test_bad_header_is_invalid);
    RUN_TEST(test_length_above_rom_limit_is_invalid);
    RUN_TEST(test_buffer_longer_than_header_length_is_ok);
    RUN_TEST(test_write_value_multiple_pairs_in_order);
    RUN_TEST(test_write_flags_follow_rm_table);
    RUN_TEST(test_write_widths_1_and_2_are_passed_through);
    RUN_TEST(test_invalid_write_rejects_whole_dcd_before_any_write);
    RUN_TEST(test_value_wider_than_width_is_invalid);
    RUN_TEST(test_malformed_write_commands_are_invalid);
    RUN_TEST(test_command_crossing_dcd_end_is_invalid);
    RUN_TEST(test_check_conditions_follow_rm_table);
    RUN_TEST(test_check_failing_condition_each_flavour);
    RUN_TEST(test_check_polls_until_condition);
    RUN_TEST(test_check_count_exhausted_stops_execution);
    RUN_TEST(test_check_without_count_stops_on_timeout);
    RUN_TEST(test_check_count_zero_is_nop);
    RUN_TEST(test_malformed_check_is_invalid);
    RUN_TEST(test_nop_is_accepted);
    RUN_TEST(test_nop_with_wrong_length_is_invalid);
    RUN_TEST(test_unknown_command_is_not_supported);
    RUN_TEST(test_on_command_reports_each_element_offset_before_it_runs);
    RUN_TEST(test_on_command_null_is_allowed);
    RUN_TEST(test_legacy_dcd_bin_executes_fully);
    return UNITY_END();
}
