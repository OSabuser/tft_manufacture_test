/**
 * @file  test_recovery.c
 * @brief Host-тесты чистой логики recovery_decide().
 *
 * Без аппаратных зависимостей (SRC_GPR/flash) — сценарии зеркалят таксономию
 * отказов Фазы 6 (firmware/bootloader/PLAN.md).
 */

#include "recovery.h"
#include "unity.h"

/* ── Вспомогательные конструкторы ─────────────────────────────────────── */

static struct image_version make_version(uint8_t major, uint8_t minor, uint16_t revision)
{
    struct image_version ver = {
        .iv_major    = major,
        .iv_minor    = minor,
        .iv_revision = revision,
        .iv_build_num = 0,
    };
    return ver;
}

static update_policy_slot_state_t make_valid_slot(struct image_version ver)
{
    update_policy_slot_state_t slot = { .valid = true, .version = ver };
    return slot;
}

static const update_policy_slot_state_t K_INVALID_SLOT = { .valid = false };
static const uint32_t K_THRESHOLD                      = 3U;

void setUp(void)
{
}

void tearDown(void)
{
}

/* ── BTN_2 — главнее счётчика ─────────────────────────────────────────── */

void test_btn2_held_enters_recovery_even_with_zero_attempts(void)
{
    recovery_decision_t d =
        recovery_decide(0U, K_THRESHOLD, &K_INVALID_SLOT, &K_INVALID_SLOT, true);

    TEST_ASSERT_EQUAL(RECOVERY_ENTER_RECOVERY_MODE, d.action);
}

void test_btn2_held_enters_recovery_even_with_healthy_slots(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(1, 0, 0));
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(2, 0, 0));

    recovery_decision_t d = recovery_decide(0U, K_THRESHOLD, &slot_a, &slot_b, true);

    TEST_ASSERT_EQUAL(RECOVERY_ENTER_RECOVERY_MODE, d.action);
}

/* ── Ниже порога — обычная загрузка ──────────────────────────────────── */

void test_below_threshold_is_normal_boot(void)
{
    recovery_decision_t d = recovery_decide(K_THRESHOLD - 1U, K_THRESHOLD, &K_INVALID_SLOT,
                                            &K_INVALID_SLOT, false);

    TEST_ASSERT_EQUAL(RECOVERY_NORMAL_BOOT, d.action);
}

void test_zero_attempts_is_normal_boot(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(1, 0, 0));

    recovery_decision_t d = recovery_decide(0U, K_THRESHOLD, &slot_a, &K_INVALID_SLOT, false);

    TEST_ASSERT_EQUAL(RECOVERY_NORMAL_BOOT, d.action);
}

/* ── На пороге, класс B, есть фолбэк ─────────────────────────────────── */

void test_at_threshold_with_fallback_erases_active_slot_a(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(2, 0, 0)); /* активный */
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(1, 0, 0)); /* фолбэк */

    recovery_decision_t d = recovery_decide(K_THRESHOLD, K_THRESHOLD, &slot_a, &slot_b, false);

    TEST_ASSERT_EQUAL(RECOVERY_ERASE_ACTIVE_THEN_BOOT_OTHER, d.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, d.active_slot);
}

void test_at_threshold_with_fallback_erases_active_slot_b(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(1, 0, 0)); /* фолбэк */
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(2, 0, 0)); /* активный */

    recovery_decision_t d = recovery_decide(K_THRESHOLD, K_THRESHOLD, &slot_a, &slot_b, false);

    TEST_ASSERT_EQUAL(RECOVERY_ERASE_ACTIVE_THEN_BOOT_OTHER, d.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_B, d.active_slot);
}

void test_above_threshold_still_triggers_fallback(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(2, 0, 0));
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(1, 0, 0));

    recovery_decision_t d = recovery_decide(K_THRESHOLD + 5U, K_THRESHOLD, &slot_a, &slot_b, false);

    TEST_ASSERT_EQUAL(RECOVERY_ERASE_ACTIVE_THEN_BOOT_OTHER, d.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, d.active_slot);
}

/* ── На пороге, класс C — откатываться некуда ────────────────────────── */

void test_at_threshold_without_fallback_enters_recovery(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(1, 0, 0)); /* активный, одинокий */

    recovery_decision_t d =
        recovery_decide(K_THRESHOLD, K_THRESHOLD, &slot_a, &K_INVALID_SLOT, false);

    TEST_ASSERT_EQUAL(RECOVERY_ENTER_RECOVERY_MODE, d.action);
}

void test_at_threshold_with_no_valid_slots_enters_recovery(void)
{
    recovery_decision_t d =
        recovery_decide(K_THRESHOLD, K_THRESHOLD, &K_INVALID_SLOT, &K_INVALID_SLOT, false);

    TEST_ASSERT_EQUAL(RECOVERY_ENTER_RECOVERY_MODE, d.action);
}

/* ── Точка входа ───────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_btn2_held_enters_recovery_even_with_zero_attempts);
    RUN_TEST(test_btn2_held_enters_recovery_even_with_healthy_slots);

    RUN_TEST(test_below_threshold_is_normal_boot);
    RUN_TEST(test_zero_attempts_is_normal_boot);

    RUN_TEST(test_at_threshold_with_fallback_erases_active_slot_a);
    RUN_TEST(test_at_threshold_with_fallback_erases_active_slot_b);
    RUN_TEST(test_above_threshold_still_triggers_fallback);

    RUN_TEST(test_at_threshold_without_fallback_enters_recovery);
    RUN_TEST(test_at_threshold_with_no_valid_slots_enters_recovery);

    return UNITY_END();
}
