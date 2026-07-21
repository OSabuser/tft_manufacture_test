/*
 * test_tft_app_smoke.c — Фаза 0: проверка живости host-харнесса tft_app.
 *
 * На Фазе 0 в tft_app ещё нет чистой доменной логики. Этот тест лишь
 * подтверждает, что таргет собирается clang'ом, линкуется с Unity и
 * запускается под CTest в пресетах host-*. На Фазе 1 заменяется реальными
 * доменными тестами (sul-декодер НКУ → sul_result_t, controller diff) —
 * см. firmware/tft_app/PLAN.md.
 */

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_harness_alive(void)
{
    TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_harness_alive);
    return UNITY_END();
}
