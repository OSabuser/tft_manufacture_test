/**
 * @file  test_bsp_reset.c
 * @brief Host-тесты разбора источника сброса (bsp/reset/src/reset_decode.c, §3.8).
 *
 * Тестируется ТОЛЬКО чистая часть — маска → человекочитаемая причина. Доступ к
 * SRC->SRSR (чтение/отображение бит/очистка) живёт в отдельной единице
 * трансляции reset.c: там нет логики, только регистры, и проверяется он на
 * стенде.
 *
 * Главное здесь — ПРИОРИТЕТ при нескольких взведённых битах: биты SRSR
 * залипают до явной очистки, поэтому «несколько сразу» — не экзотика, а
 * штатный случай (напр. первый старт после прошивки).
 */

#include "bsp/reset.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}

/* ── Одиночные источники ──────────────────────────────────────────────────── */

static void test_empty_mask_is_unknown(void)
{
    TEST_ASSERT_EQUAL_STRING("неизвестен", bsp_reset_source_name(BSP_RESET_SRC_NONE));
}

static void test_power_up(void)
{
    TEST_ASSERT_EQUAL_STRING("подача питания", bsp_reset_source_name(BSP_RESET_SRC_POWER_UP));
}

static void test_wdog(void)
{
    TEST_ASSERT_EQUAL_STRING("watchdog (WDOG1)", bsp_reset_source_name(BSP_RESET_SRC_WDOG));
}

static void test_wdog3_distinguished_from_wdog1(void)
{
    TEST_ASSERT_EQUAL_STRING("watchdog (WDOG3)", bsp_reset_source_name(BSP_RESET_SRC_WDOG3));
}

static void test_sw_or_lockup(void)
{
    /* Один бит на два разных события — различает только крэш-запись .noinit
     * (см. bsp/reset.h), поэтому и строка честно называет оба. */
    TEST_ASSERT_EQUAL_STRING("программный сброс или зависание ядра",
                             bsp_reset_source_name(BSP_RESET_SRC_SW_OR_LOCKUP));
}

static void test_user_pin(void)
{
    TEST_ASSERT_EQUAL_STRING("внешний сброс (вывод)",
                             bsp_reset_source_name(BSP_RESET_SRC_USER_PIN));
}

static void test_overheat(void)
{
    TEST_ASSERT_EQUAL_STRING("перегрев (температурная защита)",
                             bsp_reset_source_name(BSP_RESET_SRC_OVERHEAT));
}

static void test_jtag_variants_are_distinct(void)
{
    TEST_ASSERT_EQUAL_STRING("отладчик (JTAG HIGH-Z)", bsp_reset_source_name(BSP_RESET_SRC_JTAG));
    TEST_ASSERT_EQUAL_STRING("отладчик (программный сброс по JTAG)",
                             bsp_reset_source_name(BSP_RESET_SRC_JTAG_SW));
}

static void test_csu(void)
{
    TEST_ASSERT_EQUAL_STRING("CSU", bsp_reset_source_name(BSP_RESET_SRC_CSU));
}

static void test_every_flag_has_its_own_name(void)
{
    /* Ни один объявленный источник не должен схлопываться в «неизвестен» —
     * иначе реальный сброс на стенде остался бы без диагноза. Заодно ловит
     * забытую строку при добавлении нового флага в bsp/reset.h. */
    const uint32_t ALL[] = {
        BSP_RESET_SRC_POWER_UP, BSP_RESET_SRC_USER_PIN,     BSP_RESET_SRC_WDOG,
        BSP_RESET_SRC_WDOG3,    BSP_RESET_SRC_SW_OR_LOCKUP, BSP_RESET_SRC_OVERHEAT,
        BSP_RESET_SRC_JTAG,     BSP_RESET_SRC_JTAG_SW,      BSP_RESET_SRC_CSU,
    };

    for (size_t i = 0U; i < (sizeof(ALL) / sizeof(ALL[0])); i++)
    {
        const char *p_name = bsp_reset_source_name(ALL[i]);
        TEST_ASSERT_NOT_NULL(p_name);
        TEST_ASSERT_TRUE_MESSAGE(strcmp(p_name, "неизвестен") != 0,
                                 "источник без собственной строки");
    }
}

static void test_all_names_are_unique(void)
{
    /* Две одинаковые строки на разные источники сделали бы диагноз
     * неоднозначным ровно там, где он и нужен. */
    const uint32_t ALL[] = {
        BSP_RESET_SRC_POWER_UP, BSP_RESET_SRC_USER_PIN,     BSP_RESET_SRC_WDOG,
        BSP_RESET_SRC_WDOG3,    BSP_RESET_SRC_SW_OR_LOCKUP, BSP_RESET_SRC_OVERHEAT,
        BSP_RESET_SRC_JTAG,     BSP_RESET_SRC_JTAG_SW,      BSP_RESET_SRC_CSU,
    };
    const size_t COUNT = sizeof(ALL) / sizeof(ALL[0]);

    for (size_t i = 0U; i < COUNT; i++)
    {
        for (size_t j = i + 1U; j < COUNT; j++)
        {
            TEST_ASSERT_TRUE_MESSAGE(
                strcmp(bsp_reset_source_name(ALL[i]), bsp_reset_source_name(ALL[j])) != 0,
                "два источника с одинаковой строкой");
        }
    }
}

/* ── Приоритет при нескольких битах ───────────────────────────────────────── */

static void test_event_wins_over_power_up(void)
{
    /* Реальный случай: бит «подача питания» мог остаться от прошлого цикла
     * (SRSR залипает), а watchdog означает СОБЫТИЕ этого сброса. */
    TEST_ASSERT_EQUAL_STRING("watchdog (WDOG1)",
                             bsp_reset_source_name(BSP_RESET_SRC_POWER_UP | BSP_RESET_SRC_WDOG));
}

static void test_overheat_wins_over_everything(void)
{
    const uint32_t ALL = BSP_RESET_SRC_POWER_UP | BSP_RESET_SRC_USER_PIN | BSP_RESET_SRC_WDOG |
                         BSP_RESET_SRC_WDOG3 | BSP_RESET_SRC_SW_OR_LOCKUP | BSP_RESET_SRC_OVERHEAT |
                         BSP_RESET_SRC_JTAG | BSP_RESET_SRC_JTAG_SW | BSP_RESET_SRC_CSU;

    TEST_ASSERT_EQUAL_STRING("перегрев (температурная защита)", bsp_reset_source_name(ALL));
}

static void test_wdog_wins_over_sw_reset(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "watchdog (WDOG1)", bsp_reset_source_name(BSP_RESET_SRC_SW_OR_LOCKUP | BSP_RESET_SRC_WDOG));
}

static void test_sw_reset_wins_over_debugger_and_pins(void)
{
    TEST_ASSERT_EQUAL_STRING("программный сброс или зависание ядра",
                             bsp_reset_source_name(BSP_RESET_SRC_SW_OR_LOCKUP | BSP_RESET_SRC_JTAG |
                                                   BSP_RESET_SRC_USER_PIN));
}

static void test_user_pin_wins_over_power_up(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "внешний сброс (вывод)",
        bsp_reset_source_name(BSP_RESET_SRC_USER_PIN | BSP_RESET_SRC_POWER_UP));
}

/* ── Устойчивость ─────────────────────────────────────────────────────────── */

static void test_unknown_bits_ignored(void)
{
    /* Биты вне контракта (чужой чип, будущее расширение) не должны выдаваться
     * за известную причину. */
    TEST_ASSERT_EQUAL_STRING("неизвестен", bsp_reset_source_name(0x80000000U));

    /* ...но и не должны перебивать известный источник рядом. */
    TEST_ASSERT_EQUAL_STRING("подача питания",
                             bsp_reset_source_name(0x80000000U | BSP_RESET_SRC_POWER_UP));
}

static void test_name_never_null(void)
{
    for (uint32_t bit = 0U; bit < 32U; bit++)
    {
        TEST_ASSERT_NOT_NULL(bsp_reset_source_name(1U << bit));
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_empty_mask_is_unknown);
    RUN_TEST(test_power_up);
    RUN_TEST(test_wdog);
    RUN_TEST(test_wdog3_distinguished_from_wdog1);
    RUN_TEST(test_sw_or_lockup);
    RUN_TEST(test_user_pin);
    RUN_TEST(test_overheat);
    RUN_TEST(test_jtag_variants_are_distinct);
    RUN_TEST(test_csu);
    RUN_TEST(test_every_flag_has_its_own_name);
    RUN_TEST(test_all_names_are_unique);

    RUN_TEST(test_event_wins_over_power_up);
    RUN_TEST(test_overheat_wins_over_everything);
    RUN_TEST(test_wdog_wins_over_sw_reset);
    RUN_TEST(test_sw_reset_wins_over_debugger_and_pins);
    RUN_TEST(test_user_pin_wins_over_power_up);

    RUN_TEST(test_unknown_bits_ignored);
    RUN_TEST(test_name_never_null);

    return UNITY_END();
}
