/**
 * @file  test_boot_screen.c
 * @brief Host-тесты компоновщика загрузочного экрана (§3.10).
 *
 * Проверяется ЧИСТАЯ часть: структура → строки. Главное здесь не
 * форматирование как таковое, а два свойства, которые легко потерять при
 * правках:
 *   - отсутствующие данные НЕ дают строк-заготовок («Стиль: (нет)»);
 *   - недоступные данные помечаются «н/д», а не выдумываются.
 */

#include "ui/boot_screen.h"
#include "unity.h"

#include <string.h>

static boot_screen_info_t g_s_info;
static boot_screen_lines_t g_s_lines;

void setUp(void)
{
    memset(&g_s_info, 0, sizeof(g_s_info));
    memset(&g_s_lines, 0, sizeof(g_s_lines));
}

void tearDown(void)
{
}

/** Есть ли среди строк начинающаяся с префикса. */
static bool has_prefix(const char *p_prefix)
{
    for (uint8_t i = 0U; i < g_s_lines.count; i++)
    {
        if (strncmp(g_s_lines.line[i], p_prefix, strlen(p_prefix)) == 0)
        {
            return true;
        }
    }
    return false;
}

/** Найти строку по префиксу (NULL — нет такой). */
static const char *find(const char *p_prefix)
{
    for (uint8_t i = 0U; i < g_s_lines.count; i++)
    {
        if (strncmp(g_s_lines.line[i], p_prefix, strlen(p_prefix)) == 0)
        {
            return g_s_lines.line[i];
        }
    }
    return NULL;
}

/* ── Версии ───────────────────────────────────────────────────────────────── */

static void test_app_version_rendered(void)
{
    g_s_info.app_version_valid = true;
    g_s_info.app_major         = 1U;
    g_s_info.app_minor         = 2U;
    g_s_info.app_revision      = 34U;

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("Версия: 1.2.34", find("Версия:"));
}

static void test_app_version_unknown_says_so(void)
{
    /* Экран, уверенно показывающий неверную версию, хуже экрана, который
     * признаётся, что не знает. */
    g_s_info.app_version_valid = false;

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("Версия: н/д", find("Версия:"));
}

static void test_bootloader_version_defaults_to_unknown(void)
{
    /* Загрузчик версию приложению не публикует (см. boot_screen.h) — строка
     * есть ВСЕГДА, но честная. */
    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("Загрузчик: н/д", find("Загрузчик:"));
}

static void test_bootloader_version_shown_when_available(void)
{
    /* Готовность к тому, что загрузчик начнёт её публиковать. */
    g_s_info.p_bootloader_version = "1.0.0";

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("Загрузчик: 1.0.0", find("Загрузчик:"));
}

/* ── Протокол и параметр ──────────────────────────────────────────────────── */

static void test_protocol_with_param_in_one_line(void)
{
    /* Адрес — частая причина «плата живая, экран пустой», поэтому он рядом с
     * протоколом, а не отдельной строкой. */
    g_s_info.p_protocol    = "НКУ-CAN";
    g_s_info.p_param_label = "Адрес";
    g_s_info.param_value   = 1U;

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("Протокол: НКУ-CAN, Адрес 1", find("Протокол:"));
}

static void test_protocol_without_param(void)
{
    g_s_info.p_protocol = "Демо";

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("Протокол: Демо", find("Протокол:"));
}

static void test_no_protocol_no_line(void)
{
    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_FALSE(has_prefix("Протокол:"));
}

/* ── UID ──────────────────────────────────────────────────────────────────── */

static void test_uid_grouped(void)
{
    g_s_info.uid_valid  = true;
    const uint8_t UID[] = { 0x00U, 0x11U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU };
    memcpy(g_s_info.uid, UID, sizeof(UID));

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("ID: 0011AABB CCDDEEFF", find("ID:"));
}

static void test_uid_absent_no_line(void)
{
    g_s_info.uid_valid = false;

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_FALSE(has_prefix("ID:"));
}

/* ── Причина сброса ───────────────────────────────────────────────────────── */

static void test_normal_reset_produces_no_line(void)
{
    /* КЛЮЧЕВОЕ: при штатном старте строки быть не должно вовсе — иначе на
     * каждом включении она шум, а не сигнал. */
    g_s_info.p_reset_cause = NULL;

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_FALSE(has_prefix("Сброс:"));
    TEST_ASSERT_EQUAL_UINT8(BOOT_SCREEN_NO_ALERT, g_s_lines.alert_line);
}

static void test_abnormal_reset_with_breadcrumb(void)
{
    g_s_info.p_reset_cause = "TASK STALL";
    g_s_info.p_reset_where = "render:menu-open";

    boot_screen_compose(&g_s_info, &g_s_lines);

    /* Двумя строками: вместе они не влезают по ширине окна шрифтом JBMono24
     * (33 символа), и обрезало бы хвост крошки — различающую часть. */
    TEST_ASSERT_EQUAL_STRING("Сброс: TASK STALL", find("Сброс:"));
    TEST_ASSERT_EQUAL_STRING("Место: render:menu-open", find("Место:"));
}

static void test_abnormal_reset_without_breadcrumb(void)
{
    g_s_info.p_reset_cause = "HARDFAULT";
    g_s_info.p_reset_where = NULL;

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("Сброс: HARDFAULT", find("Сброс:"));
    TEST_ASSERT_FALSE(has_prefix("Место:")); /* крошки нет — строки нет */
}

static void test_alert_marks_the_reset_line_only(void)
{
    /* Пометку ставит компоновщик, а не рендер: определять тревожность по
     * содержимому — ловушка, «Сброс» и «Стиль» начинаются одинаково. */
    g_s_info.p_reset_cause = "TASK STALL";
    g_s_info.p_style_name  = "Стиль-по-умолчанию";

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_NOT_EQUAL(BOOT_SCREEN_NO_ALERT, g_s_lines.alert_line);
    TEST_ASSERT_TRUE(g_s_lines.alert_line < g_s_lines.count);
    const char *P_ALERT_PREFIX = "Сброс:";
    TEST_ASSERT_EQUAL_INT(
        0, strncmp(g_s_lines.line[g_s_lines.alert_line], P_ALERT_PREFIX, strlen(P_ALERT_PREFIX)));
}

/* ── Слоты будущих фаз ────────────────────────────────────────────────────── */

static void test_empty_future_slots_produce_no_lines(void)
{
    /* Пустые слоты — работающий контракт, а не «TODO»: сегодня строк нет,
     * Фазы 4/5 просто подставят значения. */
    g_s_info.p_custom     = NULL;
    g_s_info.p_style_name = NULL;

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_FALSE(has_prefix("Стиль:"));
    /* Кастомная строка выводится без подписи — проверяем по числу строк:
     * должны остаться только версия + загрузчик. */
    TEST_ASSERT_EQUAL_UINT8(2U, g_s_lines.count);
}

static void test_future_slots_shown_when_filled(void)
{
    g_s_info.p_style_name = "alpaca_v2";
    g_s_info.p_custom     = "ООО Ромашка";

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_EQUAL_STRING("Стиль: alpaca_v2", find("Стиль:"));
    TEST_ASSERT_TRUE(has_prefix("ООО Ромашка")); /* без подписи — строка клиента */
}

/* ── Устойчивость ─────────────────────────────────────────────────────────── */

static void test_never_exceeds_line_capacity(void)
{
    g_s_info.app_version_valid    = true;
    g_s_info.p_bootloader_version = "1.0.0";
    g_s_info.p_panel              = "TFT8";
    g_s_info.p_protocol           = "УИМ-6100";
    g_s_info.p_param_label        = "Адрес";
    g_s_info.uid_valid            = true;
    g_s_info.p_reset_cause        = "TASK STALL";
    g_s_info.p_reset_where        = "render:menu-open";
    g_s_info.p_style_name         = "style";
    g_s_info.p_custom             = "custom";

    boot_screen_compose(&g_s_info, &g_s_lines);

    TEST_ASSERT_TRUE(g_s_lines.count <= BOOT_SCREEN_MAX_LINES);
    for (uint8_t i = 0U; i < g_s_lines.count; i++)
    {
        /* Каждая строка обязана быть нуль-терминирована в пределах буфера. */
        TEST_ASSERT_TRUE(strlen(g_s_lines.line[i]) < BOOT_SCREEN_LINE_LEN);
    }
}

static void test_long_custom_string_truncated_not_overflowed(void)
{
    static char long_str[200];
    memset(long_str, 'X', sizeof(long_str) - 1U);
    long_str[sizeof(long_str) - 1U] = '\0';
    g_s_info.p_custom               = long_str;

    boot_screen_compose(&g_s_info, &g_s_lines);

    const char *p_line = g_s_lines.line[g_s_lines.count - 1U];
    TEST_ASSERT_TRUE(strlen(p_line) < BOOT_SCREEN_LINE_LEN);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_app_version_rendered);
    RUN_TEST(test_app_version_unknown_says_so);
    RUN_TEST(test_bootloader_version_defaults_to_unknown);
    RUN_TEST(test_bootloader_version_shown_when_available);

    RUN_TEST(test_protocol_with_param_in_one_line);
    RUN_TEST(test_protocol_without_param);
    RUN_TEST(test_no_protocol_no_line);

    RUN_TEST(test_uid_grouped);
    RUN_TEST(test_uid_absent_no_line);

    RUN_TEST(test_normal_reset_produces_no_line);
    RUN_TEST(test_abnormal_reset_with_breadcrumb);
    RUN_TEST(test_abnormal_reset_without_breadcrumb);
    RUN_TEST(test_alert_marks_the_reset_line_only);

    RUN_TEST(test_empty_future_slots_produce_no_lines);
    RUN_TEST(test_future_slots_shown_when_filled);

    RUN_TEST(test_never_exceeds_line_capacity);
    RUN_TEST(test_long_custom_string_truncated_not_overflowed);

    return UNITY_END();
}
