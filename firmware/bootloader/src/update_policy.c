/**
 * @file  update_policy.c
 * @brief Реализация — см. update_policy.h.
 */

#include "update_policy.h"

/* ── Сравнение версий ──────────────────────────────────────────────────── */

int image_version_compare(const struct image_version *p_ver1, const struct image_version *p_ver2)
{
    if (p_ver1->iv_major != p_ver2->iv_major)
    {
        return (p_ver1->iv_major > p_ver2->iv_major) ? 1 : -1;
    }

    if (p_ver1->iv_minor != p_ver2->iv_minor)
    {
        return (p_ver1->iv_minor > p_ver2->iv_minor) ? 1 : -1;
    }

    if (p_ver1->iv_revision != p_ver2->iv_revision)
    {
        return (p_ver1->iv_revision > p_ver2->iv_revision) ? 1 : -1;
    }

    return 0;
}

/* ── Определение активного слота ──────────────────────────────────────── */

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

static update_policy_slot_t other_slot(update_policy_slot_t slot)
{
    return (slot == UPDATE_POLICY_SLOT_A) ? UPDATE_POLICY_SLOT_B : UPDATE_POLICY_SLOT_A;
}

/* ── Публичный API ─────────────────────────────────────────────────────── */

update_policy_result_t update_policy_decide(const update_policy_slot_state_t *p_slot_a,
                                            const update_policy_slot_state_t *p_slot_b,
                                            const struct image_version *p_candidate_ver,
                                            bool button_held, bool recovery_mode)
{
    if (recovery_mode)
    {
        /* Ослабленный гейт: версия/кнопка не участвуют, целевой слот всегда
         * A, Slot Б обязан быть стёрт (см. recovery.h — этот флаг не связан
         * с recovery_decide() там). */
        return (update_policy_result_t) { .action                = UPDATE_POLICY_INSTALL,
                                          .target_slot           = UPDATE_POLICY_SLOT_A,
                                          .erase_previous_active = true };
    }

    active_slot_info_t active = find_active_slot(p_slot_a, p_slot_b);

    if (!active.have_active)
    {
        return (update_policy_result_t) { .action                = UPDATE_POLICY_INSTALL,
                                          .target_slot           = UPDATE_POLICY_SLOT_A,
                                          .erase_previous_active = false };
    }

    int cmp = image_version_compare(p_candidate_ver, &active.active_ver);

    bool candidate_newer  = (cmp > 0);
    bool forced_downgrade = (cmp < 0) && button_held;

    if (!candidate_newer && !forced_downgrade)
    {
        return (update_policy_result_t) { .action = UPDATE_POLICY_SKIP };
    }

    /* Форс. даунгрейд без стирания прежнего активного слота не имел бы
     * эффекта — он остаётся валиден и новее, и снова выиграет в boot_go().
     * "Новее" не требует стирания — прежний активный сам проиграет
     * сравнение версий естественным путём. */
    return (update_policy_result_t) { .action                = UPDATE_POLICY_INSTALL,
                                      .target_slot           = other_slot(active.active_slot),
                                      .erase_previous_active = forced_downgrade };
}
