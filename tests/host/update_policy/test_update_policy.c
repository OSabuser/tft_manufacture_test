/**
 * @file  test_update_policy.c
 * @brief Host-тесты чистой логики update_policy_decide()/image_version_compare().
 *
 * Без аппаратных зависимостей — сценарии зеркалят чек-лист аппаратной
 * верификации Фазы 3 (firmware/bootloader/PLAN.md), но проверяются здесь
 * без реального SD/flash.
 */

#include "unity.h"
#include "update_policy.h"

/* ── Вспомогательные конструкторы ─────────────────────────────────────── */

static struct image_version make_version(uint8_t major, uint8_t minor, uint16_t revision,
                                         uint32_t build_num)
{
    struct image_version ver = {
        .iv_major     = major,
        .iv_minor     = minor,
        .iv_revision  = revision,
        .iv_build_num = build_num,
    };
    return ver;
}

static update_policy_slot_state_t make_valid_slot(struct image_version ver)
{
    update_policy_slot_state_t slot = { .valid = true, .version = ver };
    return slot;
}

static const update_policy_slot_state_t K_INVALID_SLOT = { .valid = false };

void setUp(void)
{
}

void tearDown(void)
{
}

/* ── image_version_compare ─────────────────────────────────────────────── */

void test_compare_major_wins(void)
{
    struct image_version v1 = make_version(2, 0, 0, 0);
    struct image_version v2 = make_version(1, 9, 9, 9);

    TEST_ASSERT_TRUE(image_version_compare(&v1, &v2) > 0);
    TEST_ASSERT_TRUE(image_version_compare(&v2, &v1) < 0);
}

void test_compare_minor_wins_when_major_equal(void)
{
    struct image_version v1 = make_version(1, 2, 0, 0);
    struct image_version v2 = make_version(1, 1, 9, 9);

    TEST_ASSERT_TRUE(image_version_compare(&v1, &v2) > 0);
}

void test_compare_revision_wins_when_major_minor_equal(void)
{
    struct image_version v1 = make_version(1, 0, 5, 0);
    struct image_version v2 = make_version(1, 0, 3, 100);

    TEST_ASSERT_TRUE(image_version_compare(&v1, &v2) > 0);
}

void test_compare_ignores_build_num(void)
{
    struct image_version v1 = make_version(1, 0, 0, 1);
    struct image_version v2 = make_version(1, 0, 0, 999);

    TEST_ASSERT_EQUAL_INT(0, image_version_compare(&v1, &v2));
}

/* ── update_policy_decide — нет валидных слотов ──────────────────────── */

void test_no_valid_slots_installs_to_slot_a(void)
{
    struct image_version candidate = make_version(1, 0, 0, 0);

    update_policy_result_t result =
        update_policy_decide(&K_INVALID_SLOT, &K_INVALID_SLOT, &candidate, false, false);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, result.target_slot);
    TEST_ASSERT_FALSE(result.erase_previous_active); /* нечего стирать */
}

void test_only_slot_b_valid_targets_slot_a(void)
{
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(1, 0, 0, 0));
    struct image_version candidate    = make_version(2, 0, 0, 0);

    update_policy_result_t result =
        update_policy_decide(&K_INVALID_SLOT, &slot_b, &candidate, false, false);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, result.target_slot);
}

/* ── update_policy_decide — кандидат новее активного ─────────────────── */

void test_candidate_newer_installs_to_inactive_slot(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(1, 0, 0, 0));
    struct image_version candidate    = make_version(2, 0, 0, 0);

    update_policy_result_t result =
        update_policy_decide(&slot_a, &K_INVALID_SLOT, &candidate, false, false);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_B, result.target_slot);
    /* "Новее" не требует стирания прежнего активного — он сам проиграет
     * сравнение версий в boot_go(). */
    TEST_ASSERT_FALSE(result.erase_previous_active);
}

/* ── update_policy_decide — кандидат старше/равен активному ──────────── */

void test_candidate_older_without_button_skips(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(2, 0, 0, 0));
    struct image_version candidate    = make_version(1, 0, 0, 0);

    update_policy_result_t result =
        update_policy_decide(&slot_a, &K_INVALID_SLOT, &candidate, false, false);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_SKIP, result.action);
}

void test_candidate_older_with_button_forces_install(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(2, 0, 0, 0));
    struct image_version candidate    = make_version(1, 0, 0, 0);

    update_policy_result_t result =
        update_policy_decide(&slot_a, &K_INVALID_SLOT, &candidate, true, false);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_B, result.target_slot);
    /* Без стирания прежнего активного (Slot A, v2) даунгрейд не имел бы
     * эффекта — Slot A остался бы валиден и новее, снова выиграл бы в
     * boot_go(), несмотря на успешную установку v1 в Slot Б. */
    TEST_ASSERT_TRUE(result.erase_previous_active);
}

void test_candidate_equal_with_button_still_skips(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(1, 0, 0, 0));
    struct image_version candidate    = make_version(1, 0, 0, 0);

    update_policy_result_t result =
        update_policy_decide(&slot_a, &K_INVALID_SLOT, &candidate, true, false);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_SKIP, result.action);
}

/* ── update_policy_decide — оба слота валидны ─────────────────────────── */

void test_active_slot_is_the_higher_version_when_both_valid(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(1, 0, 0, 0));
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(2, 0, 0, 0));
    struct image_version candidate    = make_version(3, 0, 0, 0);

    /* Активный — Б (выше версия), значит целевой слот установки — А. */
    update_policy_result_t result = update_policy_decide(&slot_a, &slot_b, &candidate, false, false);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, result.target_slot);
    TEST_ASSERT_FALSE(result.erase_previous_active);
}

/* ── update_policy_decide — форс. даунгрейд должен реально загрузиться ───
 *
 * Регрессионный тест: без erase_previous_active даунгрейд был бы запятан на
 * flash, но никогда не загружался бы — активный слот Б (v2) остаётся
 * валиден и новее, снова выигрывает в boot_go(). Проверяем инвариант на
 * уровне решения, не дожидаясь аппаратной верификации.
 */

void test_forced_downgrade_targets_and_erases_the_higher_version_slot(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(1, 0, 0, 0));
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(2, 0, 0, 0));
    struct image_version candidate    = make_version(1, 0, 0, 0);

    /* Активный — Б (v2, выше версия). Кандидат v1 старше активного, кнопка
     * удержана → форс. установка в Slot A (неактивный) + Slot Б обязан быть
     * стёрт, иначе Slot Б (всё ещё валидный v2) снова выиграет. */
    update_policy_result_t result = update_policy_decide(&slot_a, &slot_b, &candidate, true, false);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, result.target_slot);
    TEST_ASSERT_TRUE(result.erase_previous_active);
}

/* ── update_policy_decide — recovery_mode (Фаза 6b, ослабленный gate) ────
 *
 * recovery_mode == true игнорирует версию/кнопку целиком и всегда целится в
 * Slot A с erase_previous_active == true (Slot Б стирается вызывающим
 * кодом) — независимо от того, какой слот "активен" по обычным правилам.
 */

void test_recovery_mode_installs_to_slot_a_ignoring_candidate_version(void)
{
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(5, 0, 0, 0));
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(9, 0, 0, 0));
    /* Кандидат СТАРШЕ обоих слотов — в обычном режиме это был бы SKIP. */
    struct image_version candidate = make_version(1, 0, 0, 0);

    update_policy_result_t result = update_policy_decide(&slot_a, &slot_b, &candidate, false, true);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, result.target_slot);
    TEST_ASSERT_TRUE(result.erase_previous_active);
}

void test_recovery_mode_targets_slot_a_even_when_slot_a_is_the_active_one(void)
{
    /* Slot A — активный (выше версия). Обычный режим целил бы в Slot Б —
     * recovery всё равно должен ставить в Slot A (см. rationale в
     * update_policy.h: "прежде чем что-то заменить, нужно на что менять"). */
    update_policy_slot_state_t slot_a = make_valid_slot(make_version(9, 0, 0, 0));
    update_policy_slot_state_t slot_b = make_valid_slot(make_version(1, 0, 0, 0));
    struct image_version candidate    = make_version(2, 0, 0, 0);

    update_policy_result_t result = update_policy_decide(&slot_a, &slot_b, &candidate, false, true);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, result.target_slot);
    TEST_ASSERT_TRUE(result.erase_previous_active);
}

void test_recovery_mode_installs_even_with_no_valid_slots(void)
{
    struct image_version candidate = make_version(1, 0, 0, 0);

    update_policy_result_t result =
        update_policy_decide(&K_INVALID_SLOT, &K_INVALID_SLOT, &candidate, false, true);

    TEST_ASSERT_EQUAL(UPDATE_POLICY_INSTALL, result.action);
    TEST_ASSERT_EQUAL(UPDATE_POLICY_SLOT_A, result.target_slot);
    TEST_ASSERT_TRUE(result.erase_previous_active);
}

/* ── Точка входа ───────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_compare_major_wins);
    RUN_TEST(test_compare_minor_wins_when_major_equal);
    RUN_TEST(test_compare_revision_wins_when_major_minor_equal);
    RUN_TEST(test_compare_ignores_build_num);

    RUN_TEST(test_no_valid_slots_installs_to_slot_a);
    RUN_TEST(test_only_slot_b_valid_targets_slot_a);
    RUN_TEST(test_candidate_newer_installs_to_inactive_slot);
    RUN_TEST(test_candidate_older_without_button_skips);
    RUN_TEST(test_candidate_older_with_button_forces_install);
    RUN_TEST(test_candidate_equal_with_button_still_skips);
    RUN_TEST(test_active_slot_is_the_higher_version_when_both_valid);
    RUN_TEST(test_forced_downgrade_targets_and_erases_the_higher_version_slot);

    RUN_TEST(test_recovery_mode_installs_to_slot_a_ignoring_candidate_version);
    RUN_TEST(test_recovery_mode_targets_slot_a_even_when_slot_a_is_the_active_one);
    RUN_TEST(test_recovery_mode_installs_even_with_no_valid_slots);

    return UNITY_END();
}
