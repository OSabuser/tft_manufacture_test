/**
 * @file  test_controller.c
 * @brief Host unit-тесты controller.c — diff между кэшем и новым sul_result_t.
 *
 * Чистый C, без единого HAL-вызова — fff-моков не нужно.
 */

#include "unity.h"

#include "domain/controller.h"
#include "domain/mode_priority.h"

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

/* ── Таблица приоритетов режимов (sul_resolve_mode) ──────────────────── */

static void test_resolve_mode_normal_when_no_signals(void)
{
    const sul_result_t r = sul_default_state();
    TEST_ASSERT_EQUAL(SUL_MODE_NORMAL, sul_resolve_mode(&r));
}

static void test_resolve_mode_single_signals(void)
{
    sul_result_t r;

    r = sul_default_state();
    r.fireman = true;
    TEST_ASSERT_EQUAL(SUL_MODE_FIREMAN, sul_resolve_mode(&r));

    r = sul_default_state();
    r.fire_alarm = true;
    TEST_ASSERT_EQUAL(SUL_MODE_FIRE_ALARM, sul_resolve_mode(&r));

    r = sul_default_state();
    r.overload = true;
    TEST_ASSERT_EQUAL(SUL_MODE_OVERLOAD, sul_resolve_mode(&r));

    r = sul_default_state();
    r.seismic = true;
    TEST_ASSERT_EQUAL(SUL_MODE_SEISMIC, sul_resolve_mode(&r));

    r = sul_default_state();
    r.maintenance = true;
    TEST_ASSERT_EQUAL(SUL_MODE_MAINTENANCE, sul_resolve_mode(&r));

    r = sul_default_state();
    r.lading = true;
    TEST_ASSERT_EQUAL(SUL_MODE_LADING, sul_resolve_mode(&r));
}

/* Согласованный порядок: fireman > пожар > перегруз > сейсмо > сервис >
 * погрузка. Проверяем каждую соседнюю пару при одновременной активности. */
static void test_resolve_mode_priority_ordering(void)
{
    sul_result_t r;

    r = sul_default_state();
    r.fireman    = true;
    r.fire_alarm = true; /* fireman выигрывает у пожара */
    TEST_ASSERT_EQUAL(SUL_MODE_FIREMAN, sul_resolve_mode(&r));

    r = sul_default_state();
    r.fire_alarm = true;
    r.overload   = true; /* пожар выигрывает у перегруза */
    TEST_ASSERT_EQUAL(SUL_MODE_FIRE_ALARM, sul_resolve_mode(&r));

    r = sul_default_state();
    r.overload = true;
    r.seismic  = true; /* перегруз выигрывает у сейсмо */
    TEST_ASSERT_EQUAL(SUL_MODE_OVERLOAD, sul_resolve_mode(&r));

    r = sul_default_state();
    r.seismic     = true;
    r.maintenance = true; /* сейсмо выигрывает у сервиса */
    TEST_ASSERT_EQUAL(SUL_MODE_SEISMIC, sul_resolve_mode(&r));

    r = sul_default_state();
    r.maintenance = true;
    r.lading      = true; /* сервис выигрывает у погрузки */
    TEST_ASSERT_EQUAL(SUL_MODE_MAINTENANCE, sul_resolve_mode(&r));
}

/* ── Diff новых полей контроллера ────────────────────────────────────── */

static void test_mode_pending_on_mode_change(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    sul_result_t normal = sul_default_state();
    (void) snprintf(normal.pos, SUL_POS_BUF_LEN, "5");
    (void) controller_process(&ctx, &normal);

    sul_result_t fire = normal;
    fire.fire_alarm   = true;
    const indication_task_t task = controller_process(&ctx, &fire);

    TEST_ASSERT_TRUE(task.mode_pending);
    TEST_ASSERT_EQUAL(SUL_MODE_FIRE_ALARM, task.mode);
    TEST_ASSERT_FALSE(task.pos_pending); /* позиция не менялась */
}

/* Смена сырого сигнала, не меняющая РАЗРЕШЁННЫЙ режим (перегруз при активном
 * пожаре), не должна поднимать mode_pending. */
static void test_mode_pending_stable_when_resolved_mode_unchanged(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    sul_result_t fire = sul_default_state();
    fire.fire_alarm   = true;
    (void) controller_process(&ctx, &fire);

    sul_result_t fire_plus_overload = fire;
    fire_plus_overload.overload      = true; /* режим остаётся FIRE_ALARM */
    const indication_task_t task = controller_process(&ctx, &fire_plus_overload);

    TEST_ASSERT_FALSE(task.mode_pending);
    TEST_ASSERT_EQUAL(SUL_MODE_FIRE_ALARM, task.mode);
}

static void test_arrival_and_movement_pending_on_rising_edge(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    sul_result_t quiet = sul_default_state();
    (void) controller_process(&ctx, &quiet);

    sul_result_t event = sul_default_state();
    event.arrival      = true;
    event.movement     = true;
    indication_task_t task = controller_process(&ctx, &event);
    TEST_ASSERT_TRUE(task.arrival_pending);
    TEST_ASSERT_TRUE(task.movement_pending);

    /* Уровень держится — фронта нет, pending не выставляется повторно. */
    task = controller_process(&ctx, &event);
    TEST_ASSERT_FALSE(task.arrival_pending);
    TEST_ASSERT_FALSE(task.movement_pending);
}

static void test_next_pending_on_next_floor_change(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    sul_result_t r = sul_default_state();
    (void) controller_process(&ctx, &r);

    (void) snprintf(r.next, SUL_POS_BUF_LEN, "16");
    const indication_task_t task = controller_process(&ctx, &r);
    TEST_ASSERT_TRUE(task.next_pending);
    TEST_ASSERT_FALSE(task.pos_pending);
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

    RUN_TEST(test_resolve_mode_normal_when_no_signals);
    RUN_TEST(test_resolve_mode_single_signals);
    RUN_TEST(test_resolve_mode_priority_ordering);
    RUN_TEST(test_mode_pending_on_mode_change);
    RUN_TEST(test_mode_pending_stable_when_resolved_mode_unchanged);
    RUN_TEST(test_arrival_and_movement_pending_on_rising_edge);
    RUN_TEST(test_next_pending_on_next_floor_change);

    return UNITY_END();
}
