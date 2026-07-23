/**
 * @file  test_menu_tree.c
 * @brief Host-тест боевого дерева меню (menu_tree.c) — секция "Протокол"
 *        строится из sul_settings_desc_t активного протокола, не хардкодом
 *        (ARCH §8, Фаза 3.3).
 */

#include "domain/sul.h"
#include "menu/menu_tree.h"
#include "services/settings_store.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* T_PROTO — первый ребёнок корня (гарантия menu.h: items[MENU_ROOT_INDEX] —
 * корневой SUBMENU). T_PROTO_PARAM — следующий по порядку в дереве
 * (menu_tree.c, T_ROOT/T_PROTO/T_PROTO_PARAM/T_LOG/T_EXIT) — единственное,
 * что этот тест знает о внутреннем порядке конкретного дерева. */
static uint8_t proto_index(void)
{
    return menu_tree_items()[MENU_ROOT_INDEX].first_child;
}

static void test_protocol_section_built_from_nku_can_descriptor(void)
{
    settings_t s;
    memset(&s, 0, sizeof(s));

    sul_registry_set_active(SUL_PROTOCOL_NKU_CAN);
    menu_tree_refresh_protocol_section(&s);

    const menu_item_desc_t *p_items = menu_tree_items();
    const uint8_t T_PROTO           = proto_index();
    const uint8_t T_PROTO_PARAM     = (uint8_t) (T_PROTO + 1U);

    TEST_ASSERT_EQUAL_UINT8(1U, p_items[T_PROTO].max); /* 2 протокола в реестре: НКУ-CAN + демо */
    TEST_ASSERT_EQUAL_STRING("НКУ-CAN", p_items[T_PROTO].options[0]);
    TEST_ASSERT_EQUAL_STRING("Демо", p_items[T_PROTO].options[1]);

    TEST_ASSERT_EQUAL_STRING("Адрес", p_items[T_PROTO_PARAM].label);
    TEST_ASSERT_EQUAL(MENU_BYTE, p_items[T_PROTO_PARAM].type);
    TEST_ASSERT_EQUAL_UINT8(0U, p_items[T_PROTO_PARAM].min);
    TEST_ASSERT_EQUAL_UINT8(15U, p_items[T_PROTO_PARAM].max);
    TEST_ASSERT_EQUAL_UINT16((uint16_t) offsetof(settings_t, user.proto_slice[0]),
                              p_items[T_PROTO_PARAM].value_offset);
}

static void test_protocol_section_switches_to_demo_descriptor(void)
{
    settings_t s;
    memset(&s, 0, sizeof(s));

    sul_registry_set_active(SUL_PROTOCOL_DEMO);
    menu_tree_refresh_protocol_section(&s);

    const menu_item_desc_t *p_items = menu_tree_items();
    const uint8_t T_PROTO_PARAM     = (uint8_t) (proto_index() + 1U);

    TEST_ASSERT_EQUAL_STRING("Скорость", p_items[T_PROTO_PARAM].label);
    TEST_ASSERT_EQUAL(MENU_SELECT, p_items[T_PROTO_PARAM].type);
    TEST_ASSERT_EQUAL_UINT8(0U, p_items[T_PROTO_PARAM].min);
    TEST_ASSERT_EQUAL_UINT8(2U, p_items[T_PROTO_PARAM].max);
    TEST_ASSERT_NOT_NULL(p_items[T_PROTO_PARAM].options);

    sul_registry_set_active(SUL_PROTOCOL_NKU_CAN); /* не оставлять активный реестр на демо */
}

/* Значение proto_slice[0], "протухшее" от протокола с более широким диапазоном
 * (адрес НКУ-CAN 0..15), должно клампиться под диапазон демо (0..2) при
 * переключении — иначе рендер читал бы options[value] за пределами массива
 * меток демо (см. комментарий в menu_tree_refresh_protocol_section()). */
static void test_stale_value_clamped_on_protocol_switch(void)
{
    settings_t s;
    memset(&s, 0, sizeof(s));

    sul_registry_set_active(SUL_PROTOCOL_NKU_CAN);
    menu_tree_refresh_protocol_section(&s);
    s.user.proto_slice[0] = 15U; /* валидный адрес НКУ-CAN */

    sul_registry_set_active(SUL_PROTOCOL_DEMO);
    menu_tree_refresh_protocol_section(&s);

    TEST_ASSERT_EQUAL_UINT8(2U, s.user.proto_slice[0]); /* клампится к max демо, не остаётся 15 */

    sul_registry_set_active(SUL_PROTOCOL_NKU_CAN);
}

/* Конец-в-конец: правка пункта, построенного из дескриптора, действительно
 * попадает в то самое поле settings_t, которое назвал дескриптор протокола —
 * не только структура данных совпадает, но и реальный edit через menu.c. */
static void test_protocol_param_edits_correct_settings_field(void)
{
    settings_t s;
    memset(&s, 0, sizeof(s));

    sul_registry_set_active(SUL_PROTOCOL_NKU_CAN);
    menu_tree_refresh_protocol_section(&s);

    menu_ctx_t ctx;
    menu_init(&ctx, menu_tree_items(), menu_tree_count(), &s);
    menu_open(&ctx);
    menu_next(&ctx); /* T_PROTO -> T_PROTO_PARAM */

    menu_action(&ctx); /* инкремент адреса 0 -> 1 */

    TEST_ASSERT_EQUAL_UINT8(1U, s.user.proto_slice[0]);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_protocol_section_built_from_nku_can_descriptor);
    RUN_TEST(test_protocol_section_switches_to_demo_descriptor);
    RUN_TEST(test_stale_value_clamped_on_protocol_switch);
    RUN_TEST(test_protocol_param_edits_correct_settings_field);

    return UNITY_END();
}
