/**
 * @file  test_menu.c
 * @brief Host-тесты чистой модели меню (menu.c). См. README.md.
 */

#include "menu/menu.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

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

static const menu_item_desc_t K_TREE_FLAT[A_COUNT] = {
    [A_ROOT]  = { "Настройки", MENU_SUBMENU, 0, 0, 0, MENU_ROOT_INDEX, A_PROTO, A_EXIT },
    [A_PROTO] = { "Протокол", MENU_SELECT, offsetof(settings_t, device.protocol_id), 0, 1,
                  MENU_ROOT_INDEX, 0, 0 },
    [A_ADDR]  = { "Адрес", MENU_BYTE, offsetof(settings_t, user.proto_slice[0]), 0, 15,
                  MENU_ROOT_INDEX, 0, 0 },
    [A_LOG]   = { "Логи", MENU_BOOL, offsetof(settings_t, device.log_enabled), 0, 1,
                  MENU_ROOT_INDEX, 0, 0 },
    [A_EXIT]  = { "Выход", MENU_BACK, 0, 0, 0, MENU_ROOT_INDEX, 0, 0 },
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
    [B_ROOT] = { "Настройки", MENU_SUBMENU, 0, 0, 0, MENU_ROOT_INDEX, B_A, B_EXIT },
    [B_A]    = { "A", MENU_BYTE, offsetof(settings_t, user.proto_slice[0]), 0, 9, MENU_ROOT_INDEX, 0,
                 0 },
    [B_SUB]  = { "Подменю", MENU_SUBMENU, 0, 0, 0, MENU_ROOT_INDEX, B_B, B_BACK },
    [B_EXIT] = { "Выход", MENU_BACK, 0, 0, 0, MENU_ROOT_INDEX, 0, 0 },
    [B_B]    = { "B", MENU_BOOL, offsetof(settings_t, device.log_enabled), 0, 1, B_SUB, 0, 0 },
    [B_BACK] = { "Назад", MENU_BACK, 0, 0, 0, B_SUB, 0, 0 },
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

    RUN_TEST(test_enter_submenu);
    RUN_TEST(test_back_from_submenu_returns_to_parent_item);
    RUN_TEST(test_submenu_edit_persists_and_root_exit_saves);

    return UNITY_END();
}
