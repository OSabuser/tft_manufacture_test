/**
 * @file  recovery.c
 * @brief Реализация — см. recovery.h.
 */

#include "recovery.h"

/* ── Определение активного слота ──────────────────────────────────────────
 * Тот же приём, что find_active_slot() в update_policy.c (private там) —
 * не шарим напрямую, копия минимальна и независимо тестируется здесь же,
 * как уже принято в проекте для мелкой логики между модулями. */

typedef struct
{
    bool have_active;
    update_policy_slot_t active_slot;
    struct image_version active_ver;
} active_slot_info_t;

static active_slot_info_t find_active_slot(const update_policy_slot_state_t *p_slot_a,
                                           const update_policy_slot_state_t *p_slot_b)
{
    active_slot_info_t info = { .have_active = false };

    if (p_slot_a->valid)
    {
        info.have_active = true;
        info.active_slot = UPDATE_POLICY_SLOT_A;
        info.active_ver  = p_slot_a->version;
    }

    if (p_slot_b->valid &&
        (!info.have_active || image_version_compare(&p_slot_b->version, &info.active_ver) > 0))
    {
        info.have_active = true;
        info.active_slot = UPDATE_POLICY_SLOT_B;
        info.active_ver  = p_slot_b->version;
    }

    return info;
}

static bool other_slot_valid(update_policy_slot_t active_slot,
                             const update_policy_slot_state_t *p_slot_a,
                             const update_policy_slot_state_t *p_slot_b)
{
    return (active_slot == UPDATE_POLICY_SLOT_A) ? p_slot_b->valid : p_slot_a->valid;
}

/* ── Публичный API ─────────────────────────────────────────────────────── */

recovery_decision_t recovery_decide(uint32_t attempt_count, uint32_t threshold,
                                    const update_policy_slot_state_t *p_slot_a,
                                    const update_policy_slot_state_t *p_slot_b, bool btn2_held)
{
    if (btn2_held)
    {
        return (recovery_decision_t) { .action = RECOVERY_ENTER_RECOVERY_MODE };
    }

    if (attempt_count < threshold)
    {
        return (recovery_decision_t) { .action = RECOVERY_NORMAL_BOOT };
    }

    active_slot_info_t active = find_active_slot(p_slot_a, p_slot_b);

    if (active.have_active && other_slot_valid(active.active_slot, p_slot_a, p_slot_b))
    {
        return (recovery_decision_t) { .action      = RECOVERY_ERASE_ACTIVE_THEN_BOOT_OTHER,
                                       .active_slot = active.active_slot };
    }

    return (recovery_decision_t) { .action = RECOVERY_ENTER_RECOVERY_MODE };
}
