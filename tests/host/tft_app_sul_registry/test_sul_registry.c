/**
 * @file  test_sul_registry.c
 * @brief Host-тесты реестра драйверов СУЛ (sul_registry.c) — выбор активного
 *        протокола из настроек (ARCH §8, Фаза 3.3), 2 зарегистрированных
 *        драйвера (НКУ-CAN, демо).
 */

#include "domain/sul.h"
#include "unity.h"

void setUp(void)
{
    sul_registry_set_active(SUL_PROTOCOL_NKU_CAN); /* известное состояние перед каждым тестом */
}

void tearDown(void) {}

static void test_active_defaults_to_nku_can(void)
{
    const sul_driver_t *p_active = sul_registry_active();
    TEST_ASSERT_NOT_NULL(p_active);
    TEST_ASSERT_EQUAL_UINT8(SUL_PROTOCOL_NKU_CAN, p_active->id);
}

static void test_find_returns_null_for_unknown_id(void)
{
    TEST_ASSERT_NULL(sul_registry_find(0xFFU));
}

static void test_find_returns_demo(void)
{
    const sul_driver_t *p_drv = sul_registry_find(SUL_PROTOCOL_DEMO);
    TEST_ASSERT_NOT_NULL(p_drv);
    TEST_ASSERT_EQUAL_STRING("Демо", p_drv->p_name);
}

static void test_set_active_switches_between_protocols(void)
{
    sul_registry_set_active(SUL_PROTOCOL_DEMO);
    TEST_ASSERT_EQUAL_UINT8(SUL_PROTOCOL_DEMO, sul_registry_active()->id);

    sul_registry_set_active(SUL_PROTOCOL_NKU_CAN);
    TEST_ASSERT_EQUAL_UINT8(SUL_PROTOCOL_NKU_CAN, sul_registry_active()->id);
}

static void test_set_active_ignores_unknown_id(void)
{
    sul_registry_set_active(SUL_PROTOCOL_DEMO); /* известное состояние, не дефолт */
    sul_registry_set_active(0xFFU);             /* неизвестный id — игнорируется */
    TEST_ASSERT_EQUAL_UINT8(SUL_PROTOCOL_DEMO, sul_registry_active()->id);
}

static void test_count_matches_registered_drivers(void)
{
    TEST_ASSERT_EQUAL_UINT8(2U, sul_registry_count()); /* НКУ-CAN + демо */
}

static void test_nku_can_settings_descriptor_present(void)
{
    const sul_driver_t *p_drv = sul_registry_find(SUL_PROTOCOL_NKU_CAN);
    TEST_ASSERT_NOT_NULL(p_drv->p_settings);
    TEST_ASSERT_EQUAL_UINT8(1U, p_drv->p_settings->count);
    TEST_ASSERT_EQUAL_UINT8(0U, p_drv->p_settings->p_entries[0].min);
    TEST_ASSERT_EQUAL_UINT8(15U, p_drv->p_settings->p_entries[0].max);
    TEST_ASSERT_EQUAL(SUL_SETTINGS_BYTE, p_drv->p_settings->p_entries[0].type);
}

/* Демо даёт СВОЙ дескриптор непохожей формы (SELECT с метками, не BYTE) —
 * тест того, что дескрипторный механизм не завязан на "адрес"-подобный
 * параметр НКУ-CAN (см. HANDOFF_PHASE3_TAIL.md, п.1). */
static void test_demo_settings_descriptor_present(void)
{
    const sul_driver_t *p_drv = sul_registry_find(SUL_PROTOCOL_DEMO);
    TEST_ASSERT_NOT_NULL(p_drv->p_settings);
    TEST_ASSERT_EQUAL_UINT8(1U, p_drv->p_settings->count);
    TEST_ASSERT_EQUAL(SUL_SETTINGS_SELECT, p_drv->p_settings->p_entries[0].type);
    TEST_ASSERT_EQUAL_UINT8(0U, p_drv->p_settings->p_entries[0].min);
    TEST_ASSERT_EQUAL_UINT8(2U, p_drv->p_settings->p_entries[0].max);
    TEST_ASSERT_NOT_NULL(p_drv->p_settings->p_entries[0].p_options);
}

/* Generic-канал протокол→settings (§3.5) — НКУ-CAN его использует (удалённая
 * адресация), демо — нет. task_sul_rx.c проверяет только NULL/не-NULL, без
 * ветки по id — вот что это гарантирует. */
static void test_take_pending_write_wired_only_for_nku_can(void)
{
    TEST_ASSERT_NOT_NULL(sul_registry_find(SUL_PROTOCOL_NKU_CAN)->take_pending_write);
    TEST_ASSERT_NULL(sul_registry_find(SUL_PROTOCOL_DEMO)->take_pending_write);
}

/* Таймаут "потери связи" — свойство протокола, не пользовательская настройка
 * (жёстко задаётся при регистрации). НКУ-CAN шлёт периодически — таймаут
 * активен; демо — синтетический источник, обрыва не бывает по определению. */
static void test_connection_timeout_set_per_protocol(void)
{
    TEST_ASSERT_EQUAL_UINT32(3000U, sul_registry_find(SUL_PROTOCOL_NKU_CAN)->connection_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(SUL_CONNECTION_TIMEOUT_DISABLED,
                             sul_registry_find(SUL_PROTOCOL_DEMO)->connection_timeout_ms);
}

static void test_each_driver_has_own_ctx(void)
{
    sul_registry_init();

    const sul_driver_t *p_nku  = sul_registry_find(SUL_PROTOCOL_NKU_CAN);
    const sul_driver_t *p_demo = sul_registry_find(SUL_PROTOCOL_DEMO);

    TEST_ASSERT_NOT_NULL(p_nku->p_ctx);
    TEST_ASSERT_NOT_NULL(p_demo->p_ctx);
    TEST_ASSERT_NOT_EQUAL(p_nku->p_ctx, p_demo->p_ctx);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_active_defaults_to_nku_can);
    RUN_TEST(test_find_returns_null_for_unknown_id);
    RUN_TEST(test_find_returns_demo);
    RUN_TEST(test_set_active_switches_between_protocols);
    RUN_TEST(test_set_active_ignores_unknown_id);
    RUN_TEST(test_count_matches_registered_drivers);
    RUN_TEST(test_nku_can_settings_descriptor_present);
    RUN_TEST(test_demo_settings_descriptor_present);
    RUN_TEST(test_take_pending_write_wired_only_for_nku_can);
    RUN_TEST(test_connection_timeout_set_per_protocol);
    RUN_TEST(test_each_driver_has_own_ctx);

    return UNITY_END();
}
