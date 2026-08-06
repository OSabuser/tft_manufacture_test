/**
 * @file  test_controller.c
 * @brief Host unit-тесты controller.c + mode_priority.c. См. README.md.
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

/* ── Diff контроллера ────────────────────────────────────────────────── */

static void test_first_frame_matching_default_has_no_pending(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    const sul_result_t r         = sul_default_state();
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

    r          = sul_default_state();
    r.fireman  = true;
    TEST_ASSERT_EQUAL(SUL_MODE_FIREMAN, sul_resolve_mode(&r));

    r            = sul_default_state();
    r.fire_alarm = true;
    TEST_ASSERT_EQUAL(SUL_MODE_FIRE_ALARM, sul_resolve_mode(&r));

    r            = sul_default_state();
    r.evacuation = true;
    TEST_ASSERT_EQUAL(SUL_MODE_EVACUATION, sul_resolve_mode(&r));

    r       = sul_default_state();
    r.error = true;
    TEST_ASSERT_EQUAL(SUL_MODE_ERROR, sul_resolve_mode(&r));

    r          = sul_default_state();
    r.overload = true;
    TEST_ASSERT_EQUAL(SUL_MODE_OVERLOAD, sul_resolve_mode(&r));

    r         = sul_default_state();
    r.seismic = true;
    TEST_ASSERT_EQUAL(SUL_MODE_SEISMIC, sul_resolve_mode(&r));

    r             = sul_default_state();
    r.maintenance = true;
    TEST_ASSERT_EQUAL(SUL_MODE_MAINTENANCE, sul_resolve_mode(&r));

    r        = sul_default_state();
    r.lading = true;
    TEST_ASSERT_EQUAL(SUL_MODE_LADING, sul_resolve_mode(&r));
}

static void test_resolve_mode_priority_ordering(void)
{
    sul_result_t r;

    r            = sul_default_state();
    r.fireman    = true;
    r.fire_alarm = true;
    TEST_ASSERT_EQUAL(SUL_MODE_FIREMAN, sul_resolve_mode(&r));

    r            = sul_default_state();
    r.fire_alarm = true;
    r.evacuation = true;
    TEST_ASSERT_EQUAL(SUL_MODE_FIRE_ALARM, sul_resolve_mode(&r));

    /* Эвакуация — life-safety, ВЫШЕ аварии (см. k_mode_priority). */
    r            = sul_default_state();
    r.evacuation = true;
    r.error      = true;
    TEST_ASSERT_EQUAL(SUL_MODE_EVACUATION, sul_resolve_mode(&r));

    /* Авария («лифт не работает») ВЫШЕ перегруза: перегруз временный. */
    r          = sul_default_state();
    r.error    = true;
    r.overload = true;
    TEST_ASSERT_EQUAL(SUL_MODE_ERROR, sul_resolve_mode(&r));

    r          = sul_default_state();
    r.overload = true;
    r.seismic  = true;
    TEST_ASSERT_EQUAL(SUL_MODE_OVERLOAD, sul_resolve_mode(&r));

    r             = sul_default_state();
    r.seismic     = true;
    r.maintenance = true;
    TEST_ASSERT_EQUAL(SUL_MODE_SEISMIC, sul_resolve_mode(&r));

    r             = sul_default_state();
    r.maintenance = true;
    r.lading      = true;
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

    sul_result_t fire            = normal;
    fire.fire_alarm              = true;
    const indication_task_t task = controller_process(&ctx, &fire);

    TEST_ASSERT_TRUE(task.mode_pending);
    TEST_ASSERT_EQUAL(SUL_MODE_FIRE_ALARM, task.mode);
    TEST_ASSERT_FALSE(task.pos_pending);
}

static void test_mode_pending_stable_when_resolved_mode_unchanged(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    sul_result_t fire = sul_default_state();
    fire.fire_alarm   = true;
    (void) controller_process(&ctx, &fire);

    sul_result_t fire_plus_overload = fire;
    fire_plus_overload.overload     = true;
    const indication_task_t task    = controller_process(&ctx, &fire_plus_overload);

    TEST_ASSERT_FALSE(task.mode_pending);
    TEST_ASSERT_EQUAL(SUL_MODE_FIRE_ALARM, task.mode);
}

static void test_arrival_and_movement_pending_on_rising_edge(void)
{
    controller_ctx_t ctx;
    controller_init(&ctx);

    sul_result_t quiet = sul_default_state();
    (void) controller_process(&ctx, &quiet);

    sul_result_t event     = sul_default_state();
    event.arrival          = true;
    event.movement         = true;
    indication_task_t task = controller_process(&ctx, &event);
    TEST_ASSERT_TRUE(task.arrival_pending);
    TEST_ASSERT_TRUE(task.movement_pending);

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
