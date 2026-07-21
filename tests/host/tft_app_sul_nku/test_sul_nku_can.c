/**
 * @file  test_sul_nku_can.c
 * @brief Host unit-тесты декодера НКУ-CAN (Фаза 1: PACKET1 + PACKET3).
 *
 * Чистый декодер, без единого HAL-вызова — никаких fff-моков не нужно,
 * golden-векторы CAN-кадров строятся прямо в тесте.
 */

#include "unity.h"

#include "domain/sul/nku_can.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Вспомогательные конструкторы кадров ─────────────────────────────── */

#define PACKET1_ID 0x506U
#define PACKET3_ID 0x508U
#define UNKNOWN_ID 0x123U

static sul_frame_t make_packet1(uint8_t arrow_bits)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[6] = arrow_bits; /* ARROW_MASK=0x03 на data[6] (DATA7) */
    return (sul_frame_t){.id = PACKET1_ID, .bus = 0, .p_data = s_data, .len = 8U};
}

static sul_frame_t make_packet3(uint8_t left, uint8_t right)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[5] = left;  /* FLOOR_MASK=0x3F на data[5] (left)  */
    s_data[6] = right; /* FLOOR_MASK=0x3F на data[6] (right) */
    return (sul_frame_t){.id = PACKET3_ID, .bus = 0, .p_data = s_data, .len = 8U};
}

/* ── PACKET1 — направление ───────────────────────────────────────────── */

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

/* Верхние биты data[6] (выше ARROW_MASK) не должны влиять на результат. */
static void test_packet1_ignores_bits_outside_mask(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet1(0xFCU | 1U); /* мусор в старших битах + arrow=1 */

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction);
}

/* ── PACKET3 — позиция ───────────────────────────────────────────────── */

static void test_packet3_two_digit_floor(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(1U, 2U); /* "1","2" -> "12" */

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("12", out.pos);
}

static void test_packet3_single_digit_via_space(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(16U, 5U); /* left=SPACE, right="5" -> "5" */

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("5", out.pos);
}

/* Легаси-квирк: left==0 трактуется как «пусто», как и SPACE — не «0» + right. */
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
    const sul_frame_t frame = make_packet3(22U, 1U); /* '-','1' -> "-1" */

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("-1", out.pos);
}

static void test_packet3_cyrillic_single_char(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(16U, 17U); /* SPACE, symbol_P -> "П" */

    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL_STRING("П", out.pos);
}

static void test_packet3_out_of_range_symbol_is_error(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    const sul_frame_t frame = make_packet3(1U, 63U); /* 63 > SYMBOL_TOTAL-1(37) */

    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &frame, &out));
}

/* ── Кадры не по протоколу ────────────────────────────────────────────── */

static void test_unknown_id_is_ignored_and_out_untouched(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    out.direction  = SUL_DIR_UP; /* сентинел, decode() не должен его тронуть */
    (void) memset(out.pos, 'X', sizeof(out.pos));

    uint8_t data[8]         = {0};
    const sul_frame_t frame = {.id = UNKNOWN_ID, .bus = 0, .p_data = data, .len = 8U};

    TEST_ASSERT_EQUAL(SUL_STATUS_IGNORED, nku_can_decode(&ctx, &frame, &out));
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction); /* не тронут */
    TEST_ASSERT_EQUAL_CHAR('X', out.pos[0]);      /* не тронут */
}

static void test_wrong_dlc_on_packet1_id_is_error(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    uint8_t data[8]         = {0};
    const sul_frame_t frame = {.id = PACKET1_ID, .bus = 0, .p_data = data, .len = 6U};

    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &frame, &out));
}

static void test_wrong_dlc_on_packet3_id_is_error(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;
    uint8_t data[8]         = {0};
    const sul_frame_t frame = {.id = PACKET3_ID, .bus = 0, .p_data = data, .len = 3U};

    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, nku_can_decode(&ctx, &frame, &out));
}

/* ── Накопление состояния между разными пакетами ─────────────────────── */

/* PACKET1 и PACKET3 несут разные поля — второй вызов обязан вернуть ПОЛНОЕ
 * накопленное состояние (и pos, и direction), а не только то, что пришло в
 * последнем кадре. Это ключевое свойство stateful-ctx декодера. */
static void test_state_accumulates_across_packet_types(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);
    sul_result_t out;

    const sul_frame_t f1 = make_packet1(1U); /* UP */
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &f1, &out));

    const sul_frame_t f3 = make_packet3(1U, 2U); /* "12" */
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, nku_can_decode(&ctx, &f3, &out));

    TEST_ASSERT_EQUAL_STRING("12", out.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction); /* пережило кадр PACKET3 */
}

static void test_init_resets_to_default(void)
{
    nku_can_ctx_t ctx;
    nku_can_init(&ctx);

    TEST_ASSERT_EQUAL_STRING("--", ctx.state.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, ctx.state.direction);
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

    RUN_TEST(test_state_accumulates_across_packet_types);
    RUN_TEST(test_init_resets_to_default);

    return UNITY_END();
}
