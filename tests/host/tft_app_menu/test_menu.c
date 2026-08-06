/**
 * @file  test_menu.c
 * @brief Host-тесты чистой модели меню (menu.c). См. README.md.
 */

#include "menu/menu.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}

/* ── Плоское дерево: Протокол / Адрес / Логи / Выход ─────────────────────── */

enum
{
    A_ROOT = 0,
    A_PROTO,
    A_ADDR,
    A_LOG,
    A_EXIT,
    A_COUNT,
};

/* Инициализаторы — ИМЕНОВАННЫЕ: у menu_item_desc_t поля добавляются по мере
 * появления редакторов (напр. gap_from/gap_to для разрывных диапазонов), и
 * позиционная форма молча разъезжается при вставке поля в середину. */
static const menu_item_desc_t K_TREE_FLAT[A_COUNT] = {
    [A_ROOT]  = { .label       = "Настройки",
                  .type        = MENU_SUBMENU,
                  .parent      = MENU_ROOT_INDEX,
                  .first_child = A_PROTO,
                  .last_child  = A_EXIT },
    [A_PROTO] = { .label        = "Протокол",
                  .type         = MENU_SELECT,
                  .value_offset = offsetof(settings_t, device.protocol_id),
                  .min          = 0U,
                  .max          = 1U,
                  .parent       = MENU_ROOT_INDEX },
    [A_ADDR]  = { .label        = "Адрес",
                  .type         = MENU_BYTE,
                  .value_offset = offsetof(settings_t, user.proto_slice[0]),
                  .min          = 0U,
                  .max          = 15U,
                  .parent       = MENU_ROOT_INDEX },
    [A_LOG]   = { .label        = "Логи",
                  .type         = MENU_BOOL,
                  .value_offset = offsetof(settings_t, device.log_enabled),
                  .min          = 0U,
                  .max          = 1U,
                  .parent       = MENU_ROOT_INDEX },
    [A_EXIT]  = { .label = "Выход", .type = MENU_BACK, .parent = MENU_ROOT_INDEX },
};

/* Разрывный диапазон (эталон — адрес УИМ-6100: 1..40 ∪ 46..50). */
enum
{
    G_ROOT = 0,
    G_ADDR,
    G_EXIT,
    G_COUNT,
};

static const menu_item_desc_t K_TREE_GAP[G_COUNT] = {
    [G_ROOT] = { .label       = "Настройки",
                 .type        = MENU_SUBMENU,
                 .parent      = MENU_ROOT_INDEX,
                 .first_child = G_ADDR,
                 .last_child  = G_EXIT },
    [G_ADDR] = { .label        = "Адрес",
                 .type         = MENU_BYTE,
                 .value_offset = offsetof(settings_t, user.proto_slice[0]),
                 .min          = 1U,
                 .max          = 50U,
                 .gap_from     = 41U,
                 .gap_to       = 45U,
                 .parent       = MENU_ROOT_INDEX },
    [G_EXIT] = { .label = "Выход", .type = MENU_BACK, .parent = MENU_ROOT_INDEX },
};

static void open_flat(menu_ctx_t *p_ctx, settings_t *p_s)
{
    memset(p_s, 0, sizeof(*p_s));
    menu_init(p_ctx, K_TREE_FLAT, A_COUNT, p_s);
    menu_open(p_ctx);
}

static void test_open_selects_first_toplevel(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_flat(&ctx, &s);

    TEST_ASSERT_TRUE(menu_is_open(&ctx));
    TEST_ASSERT_EQUAL_UINT8(A_PROTO, menu_current(&ctx));
}

static void test_next_wraps_around_level(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_flat(&ctx, &s);

    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(A_ADDR, menu_current(&ctx));
    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(A_LOG, menu_current(&ctx));
    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(A_EXIT, menu_current(&ctx));
    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(A_PROTO, menu_current(&ctx));
}

static void test_byte_increment_wraps(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_flat(&ctx, &s);
    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(A_ADDR, menu_current(&ctx));

    for (uint8_t i = 0U; i < 15U; ++i)
    {
        menu_action(&ctx);
    }
    TEST_ASSERT_EQUAL_UINT8(15U, s.user.proto_slice[0]);
    TEST_ASSERT_EQUAL_UINT8(15U, menu_read_value(&ctx, A_ADDR));
    TEST_ASSERT_TRUE(ctx.dirty);

    menu_action(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0U, s.user.proto_slice[0]);
}

static void test_bool_toggle(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_flat(&ctx, &s);
    s.device.log_enabled = 1U;
    menu_next(&ctx);
    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(A_LOG, menu_current(&ctx));

    menu_action(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0U, s.device.log_enabled);
    menu_action(&ctx);
    TEST_ASSERT_EQUAL_UINT8(1U, s.device.log_enabled);
}

static void test_exit_saves_when_dirty(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_flat(&ctx, &s);

    menu_next(&ctx);
    menu_action(&ctx);
    menu_next(&ctx);
    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(A_EXIT, menu_current(&ctx));

    menu_action(&ctx);
    TEST_ASSERT_FALSE(menu_is_open(&ctx));
    TEST_ASSERT_TRUE(ctx.save_requested);
}

static void test_exit_no_save_when_clean(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_flat(&ctx, &s);

    menu_next(&ctx);
    menu_next(&ctx);
    menu_next(&ctx);
    menu_action(&ctx);

    TEST_ASSERT_FALSE(menu_is_open(&ctx));
    TEST_ASSERT_FALSE(ctx.save_requested);
}

static void test_ops_noop_when_closed(void)
{
    menu_ctx_t ctx;
    settings_t s;
    memset(&s, 0, sizeof(s));
    menu_init(&ctx, K_TREE_FLAT, A_COUNT, &s);

    menu_next(&ctx);
    menu_action(&ctx);
    TEST_ASSERT_FALSE(menu_is_open(&ctx));
    TEST_ASSERT_FALSE(ctx.dirty);
}

/* ── Разрывный диапазон (gap_from/gap_to) ────────────────────────────────── */

static void open_gap(menu_ctx_t *p_ctx, settings_t *p_s, uint8_t start_value)
{
    memset(p_s, 0, sizeof(*p_s));
    p_s->user.proto_slice[0] = start_value;
    menu_init(p_ctx, K_TREE_GAP, G_COUNT, p_s);
    menu_open(p_ctx);
}

/** Внутри непрерывной части разрыв не влияет: 5 → 6. */
static void test_gap_does_not_affect_values_below_it(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_gap(&ctx, &s, 5U);

    menu_action(&ctx);
    TEST_ASSERT_EQUAL_UINT8(6U, s.user.proto_slice[0]);
}

/** Ключевой кейс: с последнего значения перед разрывом — сразу за разрыв. */
static void test_gap_is_skipped_upward(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_gap(&ctx, &s, 40U);

    menu_action(&ctx);
    TEST_ASSERT_EQUAL_UINT8(46U, s.user.proto_slice[0]); /* 41..45 недостижимы */
}

/** Значения внутри разрыва не появляются НИ на одном шаге полного обхода. */
static void test_gap_values_never_reachable_over_full_cycle(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_gap(&ctx, &s, 1U);

    /* 45 валидных значений (1..40 + 46..50) — обходим с запасом. */
    for (uint8_t i = 0U; i < 100U; ++i)
    {
        const uint8_t V = s.user.proto_slice[0];
        TEST_ASSERT_TRUE_MESSAGE((V < 41U) || (V > 45U), "значение из разрыва достижимо");
        TEST_ASSERT_TRUE(V >= 1U);
        TEST_ASSERT_TRUE(V <= 50U);
        menu_action(&ctx);
    }
}

/** С max — заворот на min, как и без разрыва. */
static void test_gap_wraps_from_max_to_min(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_gap(&ctx, &s, 50U);

    menu_action(&ctx);
    TEST_ASSERT_EQUAL_UINT8(1U, s.user.proto_slice[0]);
}

/** Полный цикл возвращается в исходное значение ровно за 45 нажатий. */
static void test_gap_full_cycle_length(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_gap(&ctx, &s, 1U);

    for (uint8_t i = 0U; i < 45U; ++i)
    {
        menu_action(&ctx);
    }
    TEST_ASSERT_EQUAL_UINT8(1U, s.user.proto_slice[0]);
}

/* ── Дерево с подменю ────────────────────────────────────────────────────── */

enum
{
    B_ROOT = 0,
    B_A,
    B_SUB,
    B_EXIT,
    B_B,
    B_BACK,
    B_COUNT,
};

static const menu_item_desc_t K_TREE_SUB[B_COUNT] = {
    [B_ROOT] = { .label       = "Настройки",
                 .type        = MENU_SUBMENU,
                 .parent      = MENU_ROOT_INDEX,
                 .first_child = B_A,
                 .last_child  = B_EXIT },
    [B_A]    = { .label        = "A",
                 .type         = MENU_BYTE,
                 .value_offset = offsetof(settings_t, user.proto_slice[0]),
                 .min          = 0U,
                 .max          = 9U,
                 .parent       = MENU_ROOT_INDEX },
    [B_SUB]  = { .label       = "Подменю",
                 .type        = MENU_SUBMENU,
                 .parent      = MENU_ROOT_INDEX,
                 .first_child = B_B,
                 .last_child  = B_BACK },
    [B_EXIT] = { .label = "Выход", .type = MENU_BACK, .parent = MENU_ROOT_INDEX },
    [B_B]    = { .label        = "B",
                 .type         = MENU_BOOL,
                 .value_offset = offsetof(settings_t, device.log_enabled),
                 .min          = 0U,
                 .max          = 1U,
                 .parent       = B_SUB },
    [B_BACK] = { .label = "Назад", .type = MENU_BACK, .parent = B_SUB },
};

static void open_sub(menu_ctx_t *p_ctx, settings_t *p_s)
{
    memset(p_s, 0, sizeof(*p_s));
    menu_init(p_ctx, K_TREE_SUB, B_COUNT, p_s);
    menu_open(p_ctx);
}

static void test_enter_submenu(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_sub(&ctx, &s);

    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(B_SUB, menu_current(&ctx));
    menu_action(&ctx);
    TEST_ASSERT_EQUAL_UINT8(B_B, menu_current(&ctx));
}

static void test_back_from_submenu_returns_to_parent_item(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_sub(&ctx, &s);

    menu_next(&ctx);
    menu_action(&ctx);
    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(B_BACK, menu_current(&ctx));

    menu_action(&ctx);
    TEST_ASSERT_TRUE(menu_is_open(&ctx));
    TEST_ASSERT_EQUAL_UINT8(B_SUB, menu_current(&ctx));
}

static void test_submenu_edit_persists_and_root_exit_saves(void)
{
    menu_ctx_t ctx;
    settings_t s;
    open_sub(&ctx, &s);
    s.device.log_enabled = 0U;

    menu_next(&ctx);
    menu_action(&ctx);
    menu_action(&ctx);
    TEST_ASSERT_EQUAL_UINT8(1U, s.device.log_enabled);

    menu_next(&ctx);
    menu_action(&ctx);
    menu_next(&ctx);
    TEST_ASSERT_EQUAL_UINT8(B_EXIT, menu_current(&ctx));

    menu_action(&ctx);
    TEST_ASSERT_FALSE(menu_is_open(&ctx));
    TEST_ASSERT_TRUE(ctx.save_requested);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_open_selects_first_toplevel);
    RUN_TEST(test_next_wraps_around_level);
    RUN_TEST(test_byte_increment_wraps);
    RUN_TEST(test_bool_toggle);
    RUN_TEST(test_exit_saves_when_dirty);
    RUN_TEST(test_exit_no_save_when_clean);
    RUN_TEST(test_ops_noop_when_closed);

    RUN_TEST(test_gap_does_not_affect_values_below_it);
    RUN_TEST(test_gap_is_skipped_upward);
    RUN_TEST(test_gap_values_never_reachable_over_full_cycle);
    RUN_TEST(test_gap_wraps_from_max_to_min);
    RUN_TEST(test_gap_full_cycle_length);

    RUN_TEST(test_enter_submenu);
    RUN_TEST(test_back_from_submenu_returns_to_parent_item);
    RUN_TEST(test_submenu_edit_persists_and_root_exit_saves);

    return UNITY_END();
}
