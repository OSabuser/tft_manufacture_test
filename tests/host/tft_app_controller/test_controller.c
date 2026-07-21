/**
 * @file  test_controller.c
 * @brief Host unit-тесты controller.c — diff между кэшем и новым sul_result_t.
 *
 * Чистый C, без единого HAL-вызова — fff-моков не нужно.
 */

#include "unity.h"

#include "domain/controller.h"

#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

static sul_result_t make_result(const char *p_pos, sul_direction_t dir)
{
    sul_result_t r = sul_default_state();
    (void) snprintf(r.pos, SUL_POS_BUF_LEN, "%s", p_pos);
    r.direction = dir;
    return r;
}

/* Кэш засеян дефолтом при init — результат, совпадающий с дефолтом, не
 * считается изменением (см. докстрок controller_init()). */
static void test_first_frame_matching_default_has_no_pending(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    const sul_result_t r  = sul_default_state();
    const indication_task_t task = controller_process(&ctx, &r);

    TEST_ASSERT_FALSE(task.pos_pending);
    TEST_ASSERT_FALSE(task.direction_pending);
}

static void test_first_frame_differing_from_default_marks_both_pending(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    const sul_result_t r         = make_result("12", SUL_DIR_UP);
    const indication_task_t task = controller_process(&ctx, &r);

    TEST_ASSERT_TRUE(task.pos_pending);
    TEST_ASSERT_TRUE(task.direction_pending);
}

static void test_repeated_identical_result_has_no_pending(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    const sul_result_t r = make_result("12", SUL_DIR_UP);
    (void) controller_process(&ctx, &r);

    const indication_task_t task = controller_process(&ctx, &r);

    TEST_ASSERT_FALSE(task.pos_pending);
    TEST_ASSERT_FALSE(task.direction_pending);
}

static void test_only_pos_change_marks_only_pos_pending(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    const sul_result_t r1 = make_result("12", SUL_DIR_UP);
    (void) controller_process(&ctx, &r1);

    const sul_result_t r2        = make_result("13", SUL_DIR_UP);
    const indication_task_t task = controller_process(&ctx, &r2);

    TEST_ASSERT_TRUE(task.pos_pending);
    TEST_ASSERT_FALSE(task.direction_pending);
}

static void test_only_direction_change_marks_only_direction_pending(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    const sul_result_t r1 = make_result("12", SUL_DIR_UP);
    (void) controller_process(&ctx, &r1);

    const sul_result_t r2        = make_result("12", SUL_DIR_DOWN);
    const indication_task_t task = controller_process(&ctx, &r2);

    TEST_ASSERT_FALSE(task.pos_pending);
    TEST_ASSERT_TRUE(task.direction_pending);
}

/* Таймаут связи = caller зовёт controller_process(sul_default_state()) тем
 * же путём, что и обычный кадр — отдельного API не существует (см. header). */
static void test_feeding_default_state_after_real_data_marks_both_pending(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    const sul_result_t r1 = make_result("12", SUL_DIR_UP);
    (void) controller_process(&ctx, &r1);

    const sul_result_t timeout_result = sul_default_state();
    const indication_task_t task      = controller_process(&ctx, &timeout_result);

    TEST_ASSERT_TRUE(task.pos_pending);
    TEST_ASSERT_TRUE(task.direction_pending);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_first_frame_matching_default_has_no_pending);
    RUN_TEST(test_first_frame_differing_from_default_marks_both_pending);
    RUN_TEST(test_repeated_identical_result_has_no_pending);
    RUN_TEST(test_only_pos_change_marks_only_pos_pending);
    RUN_TEST(test_only_direction_change_marks_only_direction_pending);
    RUN_TEST(test_feeding_default_state_after_real_data_marks_both_pending);

    return UNITY_END();
}
