/**
 * @file  test_sul_demo.c
 * @brief Host-тесты чистого декодера демо-протокола (domain/sul/demo/src/demo.c)
 *        — скриптованный маршрут (ARCH §8, Фаза 3.3).
 *
 * Тики-на-шаг (20/10/4 для медленно/норма/быстро) — деталь реализации
 * demo.c (k_ticks_per_step), продублирована здесь как в golden-тестах
 * nku_can (там так же захардкожены PACKET-ID) — держать в синхроне при
 * правке demo.c.
 */

#include "domain/sul/demo.h"
#include "unity.h"

#define TICKS_SLOW   20U
#define TICKS_NORMAL 10U
#define TICKS_FAST   4U

#define ROUTE_LEN 25U /* DEMO_ROUTE_LEN в demo.c — держать в синхроне */

void setUp(void) {}
void tearDown(void) {}

static sul_status_t advance(demo_ctx_t *p_ctx, sul_result_t *p_out, uint16_t ticks)
{
    sul_status_t rc = SUL_STATUS_ERR;
    for (uint16_t i = 0U; i < ticks; i++)
    {
        rc = demo_decode(p_ctx, NULL, p_out);
    }
    return rc;
}

static void test_init_shows_first_route_step(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);

    TEST_ASSERT_EQUAL_STRING("1", ctx.state.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, ctx.state.direction);
    TEST_ASSERT_FALSE(ctx.state.arrival);
    TEST_ASSERT_EQUAL_UINT8(1U, ctx.state.floor_num);
}

static void test_decode_ignores_frame_content(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);

    sul_result_t out;
    /* p_frame = NULL — decode() не должен разыменовывать его вовсе. */
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, demo_decode(&ctx, NULL, &out));
}

static void test_route_does_not_advance_before_ticks_per_step(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);

    sul_result_t out;
    (void) advance(&ctx, &out, TICKS_NORMAL - 1U); /* на 1 тик меньше порога */

    TEST_ASSERT_EQUAL_STRING("1", out.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, out.direction); /* всё ещё шаг 0, не 1 (UP) */
}

static void test_route_advances_to_second_step_at_normal_speed(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);

    sul_result_t out;
    (void) advance(&ctx, &out, TICKS_NORMAL); /* ровно порог -> шаг 1 */

    TEST_ASSERT_EQUAL_STRING("1", out.pos); /* пол не меняется... */
    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction); /* ...но кабина уже поехала вверх */
}

static void test_route_reaches_intermediate_stop_at_floor_7_going_up(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);

    sul_result_t out;
    (void) advance(&ctx, &out, TICKS_NORMAL * 7U); /* шаг 7 — остановка на 7 вверх */

    TEST_ASSERT_EQUAL_STRING("7", out.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, out.direction);
    TEST_ASSERT_TRUE(out.arrival);
    TEST_ASSERT_EQUAL_UINT8(7U, out.floor_num);
}

static void test_route_reaches_top_floor_11(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);

    sul_result_t out;
    (void) advance(&ctx, &out, TICKS_NORMAL * 12U); /* шаг 12 — верхний этаж */

    TEST_ASSERT_EQUAL_STRING("11", out.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, out.direction);
    TEST_ASSERT_TRUE(out.arrival);
    TEST_ASSERT_EQUAL_UINT8(11U, out.floor_num);
}

static void test_route_reaches_intermediate_stop_at_floor_3_going_down(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);

    sul_result_t out;
    (void) advance(&ctx, &out, TICKS_NORMAL * 21U); /* шаг 21 — остановка на 3 вниз */

    TEST_ASSERT_EQUAL_STRING("3", out.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, out.direction);
    TEST_ASSERT_TRUE(out.arrival);
    TEST_ASSERT_EQUAL_UINT8(3U, out.floor_num);
}

static void test_route_wraps_around_after_full_loop(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);

    sul_result_t out;
    /* Последний шаг (24) -> гонг на "1"; ровно ROUTE_LEN шагов дальше —
     * назад на шаг 0 (тот же этаж "1", но arrival уже снят). */
    (void) advance(&ctx, &out, TICKS_NORMAL * (ROUTE_LEN - 1U));
    TEST_ASSERT_TRUE(out.arrival); /* последний шаг маршрута перед циклом */

    (void) advance(&ctx, &out, TICKS_NORMAL);
    TEST_ASSERT_EQUAL_STRING("1", out.pos);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, out.direction);
    TEST_ASSERT_FALSE(out.arrival); /* цикл замкнулся на шаг 0, не завис на 24 */
}

static void test_set_speed_fast_advances_sooner(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);
    demo_set_speed(&ctx, 2U); /* "Быстро" */

    sul_result_t out;
    (void) advance(&ctx, &out, TICKS_FAST);

    TEST_ASSERT_EQUAL(SUL_DIR_UP, out.direction); /* уже шаг 1 при вчетверо меньшем числе тиков */
}

static void test_set_speed_clamps_out_of_range(void)
{
    demo_ctx_t ctx;
    demo_init(&ctx);
    demo_set_speed(&ctx, 99U);

    TEST_ASSERT_EQUAL_UINT8(2U, ctx.speed_idx); /* клампится к максимуму (Быстро) */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_shows_first_route_step);
    RUN_TEST(test_decode_ignores_frame_content);
    RUN_TEST(test_route_does_not_advance_before_ticks_per_step);
    RUN_TEST(test_route_advances_to_second_step_at_normal_speed);
    RUN_TEST(test_route_reaches_intermediate_stop_at_floor_7_going_up);
    RUN_TEST(test_route_reaches_top_floor_11);
    RUN_TEST(test_route_reaches_intermediate_stop_at_floor_3_going_down);
    RUN_TEST(test_route_wraps_around_after_full_loop);
    RUN_TEST(test_set_speed_fast_advances_sooner);
    RUN_TEST(test_set_speed_clamps_out_of_range);

    return UNITY_END();
}
