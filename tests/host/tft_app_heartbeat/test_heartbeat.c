/**
 * @file  test_heartbeat.c
 * @brief Host-тесты супервизора живости задач (app/heartbeat.c, Фаза 3.7) —
 *        единственного основания кормить watchdog.
 *
 * Модуль чистый (без FreeRTOS/bsp) и время получает параметром, поэтому вся
 * логика «кормить/не кормить» проверяется здесь, а не на стенде: на плате
 * этот код проявляется только сбросом, воспроизводить который дорого.
 *
 * Пороги (K_TASKS в heartbeat.c) продублированы ниже константами — тот же
 * приём, что в test_sul_demo.c с тиками-на-шаг: держать в синхроне при правке.
 */

#include "heartbeat.h"
#include "unity.h"

#define SUL_RX_PERIOD_MS 2000U
#define MENU_PERIOD_MS   1000U
#define MENU_GUARD_MS    3000U
#define RENDER_GUARD_MS  2000U

void setUp(void)
{
    heartbeat_reset();
}

void tearDown(void)
{
}

/** Свежи ли все — без интереса к тому, кто именно протух. */
static bool fresh(uint32_t now_ms)
{
    return heartbeat_all_fresh(now_ms, NULL, NULL);
}

/** Отметить все периодические задачи разом — типовая «система жива» точка. */
static void mark_all(uint32_t now_ms)
{
    heartbeat_mark(HB_TASK_SUL_RX, now_ms);
    heartbeat_mark(HB_TASK_MENU, now_ms);
}

/* ── Bring-up: незапущенные задачи не блокируют кормление ─────────────────── */

static void test_nothing_started_is_fresh(void)
{
    /* Во время bring-up задач ещё нет — кормление должно идти беспрепятственно,
     * иначе плата не доживала бы до создания наблюдаемых задач. */
    TEST_ASSERT_TRUE(fresh(0U));
    TEST_ASSERT_TRUE(fresh(100000U));
}

static void test_unstarted_task_does_not_block_started_one(void)
{
    heartbeat_mark(HB_TASK_SUL_RX, 0U);

    /* menu/render не запускались — только sul_rx обязана быть свежей. */
    TEST_ASSERT_TRUE(fresh(SUL_RX_PERIOD_MS - 1U));
    TEST_ASSERT_FALSE(fresh(SUL_RX_PERIOD_MS));
}

/* ── Периодическая семантика («прошла итерацию») ──────────────────────────── */

static void test_periodic_task_fresh_within_threshold(void)
{
    mark_all(1000U);

    TEST_ASSERT_TRUE(fresh(1000U));
    TEST_ASSERT_TRUE(fresh(1000U + MENU_PERIOD_MS - 1U));
}

static void test_periodic_task_stale_at_threshold(void)
{
    mark_all(1000U);

    /* menu — самый короткий порог, протухает первой. */
    hb_task_t stale = HB_TASK_RENDER;
    uint32_t age_ms = 0U;
    TEST_ASSERT_FALSE(heartbeat_all_fresh(1000U + MENU_PERIOD_MS, &stale, &age_ms));
    TEST_ASSERT_EQUAL(HB_TASK_MENU, stale);
    TEST_ASSERT_EQUAL_UINT32(MENU_PERIOD_MS, age_ms);
}

static void test_repeated_marks_keep_task_fresh(void)
{
    for (uint32_t t = 0U; t <= 10000U; t += 100U)
    {
        mark_all(t);
        TEST_ASSERT_TRUE(fresh(t));
    }
}

static void test_each_task_has_its_own_threshold(void)
{
    mark_all(0U);

    /* menu уже протухла (порог 1000), sul_rx ещё нет (порог 2000) —
     * пороги на задачу свои, а не один общий. */
    hb_task_t stale = HB_TASK_RENDER;
    TEST_ASSERT_FALSE(heartbeat_all_fresh(MENU_PERIOD_MS, &stale, NULL));
    TEST_ASSERT_EQUAL(HB_TASK_MENU, stale);

    heartbeat_mark(HB_TASK_MENU, MENU_PERIOD_MS);
    TEST_ASSERT_TRUE(fresh(MENU_PERIOD_MS));

    /* Теперь протухает sul_rx, которую не отмечали с нуля. */
    TEST_ASSERT_FALSE(heartbeat_all_fresh(SUL_RX_PERIOD_MS, &stale, NULL));
    TEST_ASSERT_EQUAL(HB_TASK_SUL_RX, stale);
}

/* ── Event-driven семантика (простой законен) ─────────────────────────────── */

static void test_event_driven_task_idle_forever_is_fresh(void)
{
    /* render_task спит на ulTaskNotifyTake(portMAX_DELAY) — при отсутствии
     * событий это ШТАТНОЕ состояние. Наивное «прошла итерацию» уронило бы
     * плату на простое; ровно эту ловушку и проверяем. */
    heartbeat_enter(HB_TASK_RENDER, 0U, "render:first-frame");
    heartbeat_leave(HB_TASK_RENDER, 10U);

    mark_all(1000000U);
    TEST_ASSERT_TRUE(fresh(1000000U)); /* render молчит ~17 минут — норма */
}

static void test_guarded_operation_fresh_within_threshold(void)
{
    mark_all(0U);
    heartbeat_enter(HB_TASK_RENDER, 0U, "render:indication");

    mark_all(RENDER_GUARD_MS - 1U);
    TEST_ASSERT_TRUE(fresh(RENDER_GUARD_MS - 1U));
}

static void test_guarded_operation_stale_at_threshold(void)
{
    mark_all(0U);
    heartbeat_enter(HB_TASK_RENDER, 0U, "render:indication");

    mark_all(RENDER_GUARD_MS);

    hb_task_t stale = HB_TASK_SUL_RX;
    uint32_t age_ms = 0U;
    TEST_ASSERT_FALSE(heartbeat_all_fresh(RENDER_GUARD_MS, &stale, &age_ms));
    TEST_ASSERT_EQUAL(HB_TASK_RENDER, stale);
    TEST_ASSERT_EQUAL_UINT32(RENDER_GUARD_MS, age_ms);
}

static void test_leave_releases_guard(void)
{
    mark_all(0U);
    heartbeat_enter(HB_TASK_RENDER, 0U, "render:menu-open");
    heartbeat_leave(HB_TASK_RENDER, 100U);

    mark_all(RENDER_GUARD_MS + 5000U);
    TEST_ASSERT_TRUE(fresh(RENDER_GUARD_MS + 5000U));
}

static void test_leave_counts_as_progress_for_periodic_task(void)
{
    /* Длинная защищённая операция у ПЕРИОДИЧЕСКОЙ задачи (menu: запись
     * настроек в QSPI). Пока она идёт, задача не отмечается — и без учёта
     * leave() как признака прогресса оказалась бы протухшей сразу по выходе,
     * при том что только что доказала живость. */
    heartbeat_mark(HB_TASK_SUL_RX, 0U);
    heartbeat_mark(HB_TASK_MENU, 0U);

    heartbeat_enter(HB_TASK_MENU, 0U, "menu:settings-save");
    heartbeat_leave(HB_TASK_MENU, MENU_GUARD_MS - 1U);

    heartbeat_mark(HB_TASK_SUL_RX, MENU_GUARD_MS - 1U);
    TEST_ASSERT_TRUE(fresh(MENU_GUARD_MS - 1U));
}

static void test_guard_threshold_wins_over_period_while_busy(void)
{
    /* Внутри защищённой операции period_ms неприменим: задача занята делом.
     * menu: period 1000, guard 3000 — на 2000 мс она обязана быть свежей. */
    heartbeat_mark(HB_TASK_SUL_RX, 0U);
    heartbeat_mark(HB_TASK_MENU, 0U);
    heartbeat_enter(HB_TASK_MENU, 0U, "menu:settings-save");

    heartbeat_mark(HB_TASK_SUL_RX, 2000U);
    TEST_ASSERT_TRUE(fresh(2000U));

    heartbeat_mark(HB_TASK_SUL_RX, MENU_GUARD_MS);
    TEST_ASSERT_FALSE(fresh(MENU_GUARD_MS));
}

/* ── Крошки и имена (попадают в крэш-запись) ──────────────────────────────── */

static void test_breadcrumb_reflects_current_operation(void)
{
    TEST_ASSERT_EQUAL_STRING("idle", heartbeat_breadcrumb(HB_TASK_RENDER));

    heartbeat_enter(HB_TASK_RENDER, 0U, "render:menu-nav");
    TEST_ASSERT_EQUAL_STRING("render:menu-nav", heartbeat_breadcrumb(HB_TASK_RENDER));

    heartbeat_leave(HB_TASK_RENDER, 10U);
    TEST_ASSERT_EQUAL_STRING("idle", heartbeat_breadcrumb(HB_TASK_RENDER));
}

static void test_task_names_are_stable(void)
{
    TEST_ASSERT_EQUAL_STRING("sul_rx", heartbeat_task_name(HB_TASK_SUL_RX));
    TEST_ASSERT_EQUAL_STRING("menu", heartbeat_task_name(HB_TASK_MENU));
    TEST_ASSERT_EQUAL_STRING("render", heartbeat_task_name(HB_TASK_RENDER));
}

static void test_out_of_range_task_is_ignored(void)
{
    heartbeat_mark(HB_TASK_COUNT, 0U);
    heartbeat_enter(HB_TASK_COUNT, 0U, "nowhere");
    heartbeat_leave(HB_TASK_COUNT, 0U);

    TEST_ASSERT_EQUAL_STRING("?", heartbeat_task_name(HB_TASK_COUNT));
    TEST_ASSERT_EQUAL_STRING("?", heartbeat_breadcrumb(HB_TASK_COUNT));
    TEST_ASSERT_TRUE(fresh(0U)); /* мусорный id не «запустил» несуществующую задачу */
}

/* ── Защёлка приговора (heartbeat_should_feed) ────────────────────────────── */

static void test_should_feed_while_all_fresh(void)
{
    mark_all(0U);
    TEST_ASSERT_TRUE(heartbeat_should_feed(0U, NULL));
    TEST_ASSERT_TRUE(heartbeat_should_feed(MENU_PERIOD_MS - 1U, NULL));
}

static void test_should_feed_stops_on_stall_and_reports_circumstances(void)
{
    mark_all(0U);
    heartbeat_enter(HB_TASK_RENDER, 0U, "render:menu-nav");
    mark_all(RENDER_GUARD_MS);

    hb_stall_t stall;
    TEST_ASSERT_FALSE(heartbeat_should_feed(RENDER_GUARD_MS, &stall));
    TEST_ASSERT_EQUAL(HB_TASK_RENDER, stall.task);
    TEST_ASSERT_EQUAL_UINT32(RENDER_GUARD_MS, stall.age_ms);
    TEST_ASSERT_EQUAL_STRING("render:menu-nav", stall.p_where);
}

static void test_stall_verdict_survives_task_recovery(void)
{
    /* ГЛАВНОЕ свойство: порог свежести (2 c) НАМНОГО меньше таймаута watchdog
     * (10 c), поэтому залипшая задача успевает «отлипнуть» в этом окне. Без
     * защёлки кормление возобновилось бы, сброса не случилось бы вовсе, а
     * крэш-запись осталась бы висеть и приписала бы себя следующему, ни при
     * чём не виноватому сбросу. */
    mark_all(0U);
    heartbeat_enter(HB_TASK_RENDER, 0U, "render:menu-nav");
    mark_all(RENDER_GUARD_MS);
    TEST_ASSERT_FALSE(heartbeat_should_feed(RENDER_GUARD_MS, NULL));

    /* Задача «выздоровела» и система снова полностью свежа... */
    heartbeat_leave(HB_TASK_RENDER, RENDER_GUARD_MS + 10U);
    mark_all(RENDER_GUARD_MS + 10U);
    TEST_ASSERT_TRUE(heartbeat_all_fresh(RENDER_GUARD_MS + 10U, NULL, NULL));

    /* ...но кормление уже не возобновляется. */
    TEST_ASSERT_FALSE(heartbeat_should_feed(RENDER_GUARD_MS + 10U, NULL));
    TEST_ASSERT_FALSE(heartbeat_should_feed(RENDER_GUARD_MS + 5000U, NULL));
}

static void test_stall_circumstances_frozen_at_verdict(void)
{
    /* Крошка к следующему вызову сменилась бы на "idle", а протухнуть успела
     * бы и вторая задача — в крэш-запись обязана попасть ПЕРВИЧНАЯ причина,
     * а не то, во что состояние выродилось потом (лавина от того же залипания). */
    mark_all(0U);
    heartbeat_enter(HB_TASK_RENDER, 0U, "render:indication");
    mark_all(RENDER_GUARD_MS);
    TEST_ASSERT_FALSE(heartbeat_should_feed(RENDER_GUARD_MS, NULL));

    heartbeat_leave(HB_TASK_RENDER, RENDER_GUARD_MS + 1U); /* крошка → "idle" */

    hb_stall_t stall;
    TEST_ASSERT_FALSE(heartbeat_should_feed(RENDER_GUARD_MS + 60000U, &stall));
    TEST_ASSERT_EQUAL(HB_TASK_RENDER, stall.task);
    TEST_ASSERT_EQUAL_UINT32(RENDER_GUARD_MS, stall.age_ms);
    TEST_ASSERT_EQUAL_STRING("render:indication", stall.p_where);
}

static void test_reset_releases_stall_latch(void)
{
    mark_all(0U);
    TEST_ASSERT_FALSE(heartbeat_should_feed(SUL_RX_PERIOD_MS, NULL));

    heartbeat_reset();
    TEST_ASSERT_TRUE(heartbeat_should_feed(0U, NULL));
}

/* ── Переворот счётчика времени ───────────────────────────────────────────── */

static void test_tick_wraparound_does_not_false_trip(void)
{
    /* Беззнаковая разность корректна через переворот uint32 — иначе на
     * ~49-е сутки непрерывной работы супервизор разом счёл бы все задачи
     * протухшими и уронил бы плату. */
    const uint32_t NEAR_MAX = 0xFFFFFF00U;

    mark_all(NEAR_MAX);
    TEST_ASSERT_TRUE(fresh(NEAR_MAX + 500U)); /* уже за переворотом */

    mark_all(NEAR_MAX + 500U);
    TEST_ASSERT_FALSE(fresh(NEAR_MAX + 500U + MENU_PERIOD_MS));
}

static void test_reset_forgets_everything(void)
{
    mark_all(0U);
    heartbeat_enter(HB_TASK_RENDER, 0U, "render:indication");

    heartbeat_reset();

    TEST_ASSERT_TRUE(fresh(1000000U));
    TEST_ASSERT_EQUAL_STRING("idle", heartbeat_breadcrumb(HB_TASK_RENDER));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_nothing_started_is_fresh);
    RUN_TEST(test_unstarted_task_does_not_block_started_one);

    RUN_TEST(test_periodic_task_fresh_within_threshold);
    RUN_TEST(test_periodic_task_stale_at_threshold);
    RUN_TEST(test_repeated_marks_keep_task_fresh);
    RUN_TEST(test_each_task_has_its_own_threshold);

    RUN_TEST(test_event_driven_task_idle_forever_is_fresh);
    RUN_TEST(test_guarded_operation_fresh_within_threshold);
    RUN_TEST(test_guarded_operation_stale_at_threshold);
    RUN_TEST(test_leave_releases_guard);
    RUN_TEST(test_leave_counts_as_progress_for_periodic_task);
    RUN_TEST(test_guard_threshold_wins_over_period_while_busy);

    RUN_TEST(test_breadcrumb_reflects_current_operation);
    RUN_TEST(test_task_names_are_stable);
    RUN_TEST(test_out_of_range_task_is_ignored);

    RUN_TEST(test_should_feed_while_all_fresh);
    RUN_TEST(test_should_feed_stops_on_stall_and_reports_circumstances);
    RUN_TEST(test_stall_verdict_survives_task_recovery);
    RUN_TEST(test_stall_circumstances_frozen_at_verdict);
    RUN_TEST(test_reset_releases_stall_latch);

    RUN_TEST(test_tick_wraparound_does_not_false_trip);
    RUN_TEST(test_reset_forgets_everything);

    return UNITY_END();
}
