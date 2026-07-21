/**
 * @file  test_sul_nku_can.c
 * @brief Host unit-тесты декодера НКУ-CAN (PACKET1..5 + адрес). См. README.md.
 */

#include "domain/sul/nku_can.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Конструкторы кадров ─────────────────────────────────────────────────── */

#define PACKET1_ID 0x506U
#define PACKET2_ID 0x408U
#define PACKET3_ID 0x508U
#define PACKET4_ID 0x50BU
#define PACKET5_ID 0x606U
#define UNKNOWN_ID 0x123U

static sul_frame_t make_packet1(uint8_t arrow_bits)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[6] = arrow_bits;
    return (sul_frame_t){ .id = PACKET1_ID, .bus = 0, .p_data = s_data, .len = 8U };
}

static sul_frame_t make_packet3(uint8_t left, uint8_t right)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[5] = left;
    s_data[6] = right;
    return (sul_frame_t){ .id = PACKET3_ID, .bus = 0, .p_data = s_data, .len = 8U };
}

static sul_frame_t make_packet1_full(uint8_t arrow, uint8_t movement, uint8_t icon_nibble,
                                     uint8_t level)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[6] = (uint8_t) ((icon_nibble & 0xF0U) | ((movement & 0x03U) << 2) | (arrow & 0x03U));
    s_data[3] = level & 0x3FU;
    return (sul_frame_t){ .id = PACKET1_ID, .bus = 0, .p_data = s_data, .len = 8U };
}

static sul_frame_t make_packet2(bool overload)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[7] = overload ? 0x40U : 0x00U;
    return (sul_frame_t){ .id = PACKET2_ID, .bus = 0, .p_data = s_data, .len = 8U };
}

static sul_frame_t make_packet3_full(uint8_t left, uint8_t right, bool gong, uint8_t secs,
                                     uint8_t mins)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[5] = left;
    s_data[6] = right;
    s_data[2] = secs & 0x3FU;
    s_data[3] = (uint8_t) ((mins & 0x0FU) | (gong ? 0x00U : 0x40U));
    return (sul_frame_t){ .id = PACKET3_ID, .bus = 0, .p_data = s_data, .len = 8U };
}

static sul_frame_t make_packet4(bool overload, bool seismic)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[5] = overload ? 0x40U : 0x00U;
    s_data[0] = seismic ? 0x80U : 0x00U;
    return (sul_frame_t){ .id = PACKET4_ID, .bus = 0, .p_data = s_data, .len = 8U };
}

static sul_frame_t make_packet5(uint8_t next_left, uint8_t next_right, uint8_t dest_level)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[0] = dest_level;
    s_data[3] = next_left;
    s_data[4] = next_right;
    return (sul_frame_t){ .id = PACKET5_ID, .bus = 0, .p_data = s_data, .len = 8U };
}

/* ── PACKET1 — направление ───────────────────────────────────────────────── */

static void test_packet1_none(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1(0U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, out.direction);
}

static void test_packet1_up(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1(1U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction);
}

static void test_packet1_down(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1(2U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_DOWN, out.direction);
}

static void test_packet1_double(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1(3U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_DOUBLE, out.direction);
}

static void test_packet1_ignores_bits_outside_mask(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1(0xFCU | 1U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction);
}

/* ── PACKET3 — позиция ───────────────────────────────────────────────────── */

static void test_packet3_two_digit_floor(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(1U, 2U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("12", out.pos);
}

static void test_packet3_single_digit_via_space(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(16U, 5U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("5", out.pos);
}

static void test_packet3_single_digit_via_zero_byte(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(0U, 7U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("7", out.pos);
}

static void test_packet3_negative_floor(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(22U, 1U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("-1", out.pos);
}

static void test_packet3_cyrillic_single_char(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(16U, 17U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("П", out.pos);
}

static void test_packet3_out_of_range_symbol_is_error(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(1U, 63U);

    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &frame, &out));
}

/* ── Кадры не по протоколу / малформированные ────────────────────────────── */

static void test_unknown_id_is_ignored_and_out_untouched(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    out.direction = SUL_DIR_UP;
    (void) memset(out.pos, 'X', sizeof(out.pos));

    uint8_t data[8]         = { 0 };
    const sul_frame_t frame = { .id = UNKNOWN_ID, .bus = 0, .p_data = data, .len = 8U };

    TEST_ASSERT_EQUAL(SUL_STATUS_IGNORED, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction);
    TEST_ASSERT_EQUAL_CHAR('X', out.pos[0]);
}

static void test_wrong_dlc_on_packet1_id_is_error(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    uint8_t data[8]         = { 0 };
    const sul_frame_t frame = { .id = PACKET1_ID, .bus = 0, .p_data = data, .len = 6U };

    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &frame, &out));
}

static void test_wrong_dlc_on_packet3_id_is_error(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    uint8_t data[8]         = { 0 };
    const sul_frame_t frame = { .id = PACKET3_ID, .bus = 0, .p_data = data, .len = 3U };

    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &frame, &out));
}

static void test_wrong_dlc_on_packet2_4_5_is_error(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    uint8_t data[8] = { 0 };

    const sul_frame_t f2 = { .id = PACKET2_ID, .bus = 0, .p_data = data, .len = 6U };
    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &f2, &out));
    const sul_frame_t f4 = { .id = PACKET4_ID, .bus = 0, .p_data = data, .len = 7U };
    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &f4, &out));
    const sul_frame_t f5 = { .id = PACKET5_ID, .bus = 0, .p_data = data, .len = 4U };
    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &f5, &out));
}

/* ── Накопление состояния ────────────────────────────────────────────────── */

static void test_state_accumulates_across_packet_types(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t f1 = make_packet1(1U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &f1, &out));

    const sul_frame_t f3 = make_packet3(1U, 2U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &f3, &out));

    TEST_ASSERT_EQUAL_STRING("12", out.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction);
}

static void test_init_resets_to_default(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);

    TEST_ASSERT_EQUAL_STRING("--", ctx.state.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, ctx.state.direction);
}

/* ── PACKET1 — режимы и начало движения ──────────────────────────────────── */

static void test_packet1_mode_fire(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1_full(0U, 0U, 0x70U, 0U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_TRUE(out.fire_alarm);
}

static void test_packet1_mode_maintenance_variants(void)
{
    const uint8_t icons[] = { 0x30U, 0x50U, 0x40U };
    for (size_t i = 0U; i < sizeof(icons); ++i)
    {
        nku_can_ctx_t ctx;
        nku_can_init(&ctx);
        sul_result_t out;
        const sul_frame_t frame = make_packet1_full(0U, 0U, icons[i], 0U);
        TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
        TEST_ASSERT_TRUE(out.maintenance);
    }
}

static void test_packet1_mode_lading_instrument(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1_full(0U, 0U, 0x10U, 0U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_TRUE(out.lading);
}

static void test_packet1_mode_fireman(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1_full(0U, 0U, 0xF0U, 0U);

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_TRUE(out.fireman);
}

static void test_packet1_normal_clears_previous_mode(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t fire = make_packet1_full(0U, 0U, 0x70U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &fire, &out));
    TEST_ASSERT_TRUE(out.fire_alarm);

    const sul_frame_t normal = make_packet1_full(1U, 0U, 0x00U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &normal, &out));
    TEST_ASSERT_FALSE(out.fire_alarm);
}

static void test_packet1_movement_bits(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t moving = make_packet1_full(1U, 1U, 0x00U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &moving, &out));
    TEST_ASSERT_TRUE(out.movement);

    const sul_frame_t still = make_packet1_full(1U, 0U, 0x00U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &still, &out));
    TEST_ASSERT_FALSE(out.movement);
}

/* ── PACKET2 / PACKET4 — перегруз (мультиисточник) и сейсмо ───────────────── */

static void test_packet2_overload_set_and_clear(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t on = make_packet2(true);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &on, &out));
    TEST_ASSERT_TRUE(out.overload);

    const sul_frame_t off = make_packet2(false);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &off, &out));
    TEST_ASSERT_FALSE(out.overload);
}

static void test_overload_multisource_independence(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t p4_on = make_packet4(true, false);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &p4_on, &out));
    TEST_ASSERT_TRUE(out.overload);

    const sul_frame_t p2_off = make_packet2(false);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &p2_off, &out));
    TEST_ASSERT_TRUE(out.overload);

    const sul_frame_t p4_off = make_packet4(false, false);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &p4_off, &out));
    TEST_ASSERT_FALSE(out.overload);
}

static void test_packet4_seismic_set_and_clear(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t on = make_packet4(false, true);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &on, &out));
    TEST_ASSERT_TRUE(out.seismic);

    const sul_frame_t off = make_packet4(false, false);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &off, &out));
    TEST_ASSERT_FALSE(out.seismic);
}

/* ── PACKET3 — гонг, временная погрузка, floor_num ───────────────────────── */

static void test_packet3_gong_active_when_bit_clear(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t gong = make_packet3_full(1U, 2U, true, 0U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &gong, &out));
    TEST_ASSERT_TRUE(out.arrival);

    const sul_frame_t no_gong = make_packet3_full(1U, 2U, false, 0U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &no_gong, &out));
    TEST_ASSERT_FALSE(out.arrival);
}

static void test_packet3_lading_time_seconds(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t frame = make_packet3_full(1U, 2U, false, 30U, 1U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_UINT16(90U, out.lading_secs);
    TEST_ASSERT_TRUE(out.lading);
}

static void test_lading_multisource_independence(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t timed = make_packet3_full(1U, 2U, false, 10U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &timed, &out));
    TEST_ASSERT_TRUE(out.lading);

    const sul_frame_t p1_normal = make_packet1_full(0U, 0U, 0x00U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &p1_normal, &out));
    TEST_ASSERT_TRUE(out.lading);

    const sul_frame_t timed_zero = make_packet3_full(1U, 2U, false, 0U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &timed_zero, &out));
    TEST_ASSERT_FALSE(out.lading);
}

static void test_packet3_floor_num_derivation(void)
{
    nku_can_ctx_t ctx;
    sul_result_t out;

    nku_can_init(&ctx);
    const sul_frame_t two_digit = make_packet3_full(1U, 2U, false, 0U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &two_digit, &out));
    TEST_ASSERT_EQUAL_UINT8(12U, out.floor_num);

    nku_can_init(&ctx);
    const sul_frame_t negative = make_packet3_full(22U, 1U, false, 0U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &negative, &out));
    TEST_ASSERT_EQUAL_UINT8(41U, out.floor_num);

    nku_can_init(&ctx);
    const sul_frame_t basement = make_packet3_full(17U, 1U, false, 0U, 0U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &basement, &out));
    TEST_ASSERT_EQUAL_UINT8(51U, out.floor_num);
}

/* ── PACKET5 — следующий этаж ────────────────────────────────────────────── */

static void test_packet5_next_shown_while_moving(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t moving = make_packet1_full(1U, 1U, 0x00U, 5U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &moving, &out));

    const sul_frame_t next = make_packet5(1U, 6U, 16U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &next, &out));
    TEST_ASSERT_EQUAL_STRING("16", out.next);
}

static void test_packet5_next_suppressed_when_not_moving(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t still = make_packet1_full(0U, 0U, 0x00U, 5U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &still, &out));

    const sul_frame_t next = make_packet5(1U, 6U, 16U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &next, &out));
    TEST_ASSERT_EQUAL_STRING("", out.next);
}

static void test_packet5_next_suppressed_when_dest_equals_level(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t moving = make_packet1_full(1U, 1U, 0x00U, 5U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &moving, &out));

    const sul_frame_t next = make_packet5(1U, 6U, 5U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &next, &out));
    TEST_ASSERT_EQUAL_STRING("", out.next);
}

static void test_packet5_next_cleared_on_arrival(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t moving = make_packet1_full(1U, 1U, 0x00U, 5U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &moving, &out));
    const sul_frame_t next = make_packet5(1U, 6U, 16U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &next, &out));
    TEST_ASSERT_EQUAL_STRING("16", out.next);

    const sul_frame_t arrived = make_packet1_full(0U, 0U, 0x00U, 16U);
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &arrived, &out));
    TEST_ASSERT_EQUAL_STRING("", out.next);
}

/* ── Адрес станции: сдвиг ID пакетов ─────────────────────────────────────── */

static void test_address_shifts_packet_ids(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    nku_can_set_address(&ctx, 1U);
    sul_result_t out;

    uint8_t data[8] = { 0 };
    data[6]         = 1U;

    const sul_frame_t shifted = { .id = 0x506U | 0x10U, .bus = 0, .p_data = data, .len = 8U };
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &shifted, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction);

    const sul_frame_t base = { .id = 0x506U, .bus = 0, .p_data = data, .len = 8U };
    TEST_ASSERT_EQUAL(SUL_STATUS_IGNORED, nku_can_decode(&ctx, &base, &out));
}

static void test_address_clamped_to_max(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    nku_can_set_address(&ctx, 200U);
    sul_result_t out;

    uint8_t data[8] = { 0 };
    data[6]         = 2U;

    const sul_frame_t f = { .id = 0x506U | 0xF0U, .bus = 0, .p_data = data, .len = 8U };
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &f, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_DOWN, out.direction);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_packet1_none);
    RUN_TEST(test_packet1_up);
    RUN_TEST(test_packet1_down);
    RUN_TEST(test_packet1_double);
    RUN_TEST(test_packet1_ignores_bits_outside_mask);

    RUN_TEST(test_packet3_two_digit_floor);
    RUN_TEST(test_packet3_single_digit_via_space);
    RUN_TEST(test_packet3_single_digit_via_zero_byte);
    RUN_TEST(test_packet3_negative_floor);
    RUN_TEST(test_packet3_cyrillic_single_char);
    RUN_TEST(test_packet3_out_of_range_symbol_is_error);

    RUN_TEST(test_unknown_id_is_ignored_and_out_untouched);
    RUN_TEST(test_wrong_dlc_on_packet1_id_is_error);
    RUN_TEST(test_wrong_dlc_on_packet3_id_is_error);
    RUN_TEST(test_wrong_dlc_on_packet2_4_5_is_error);

    RUN_TEST(test_state_accumulates_across_packet_types);
    RUN_TEST(test_init_resets_to_default);

    RUN_TEST(test_packet1_mode_fire);
    RUN_TEST(test_packet1_mode_maintenance_variants);
    RUN_TEST(test_packet1_mode_lading_instrument);
    RUN_TEST(test_packet1_mode_fireman);
    RUN_TEST(test_packet1_normal_clears_previous_mode);
    RUN_TEST(test_packet1_movement_bits);

    RUN_TEST(test_packet2_overload_set_and_clear);
    RUN_TEST(test_overload_multisource_independence);
    RUN_TEST(test_packet4_seismic_set_and_clear);

    RUN_TEST(test_packet3_gong_active_when_bit_clear);
    RUN_TEST(test_packet3_lading_time_seconds);
    RUN_TEST(test_lading_multisource_independence);
    RUN_TEST(test_packet3_floor_num_derivation);

    RUN_TEST(test_packet5_next_shown_while_moving);
    RUN_TEST(test_packet5_next_suppressed_when_not_moving);
    RUN_TEST(test_packet5_next_suppressed_when_dest_equals_level);
    RUN_TEST(test_packet5_next_cleared_on_arrival);

    RUN_TEST(test_address_shifts_packet_ids);
    RUN_TEST(test_address_clamped_to_max);

    return UNITY_END();
}
