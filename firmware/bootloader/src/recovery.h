/**
 * @file  recovery.h
 * @brief Чистая логика решения "что делать с этой попыткой загрузки" —
 *        без аппаратных зависимостей (flash/SRC_GPR), полностью
 *        host-тестируема.
 *
 * См. firmware/bootloader/PLAN.md, Фаза 6, разбивка 6a.
 */

#ifndef RECOVERY_H_
#define RECOVERY_H_

#include "update_policy.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    RECOVERY_NORMAL_BOOT = 0,
    RECOVERY_ERASE_ACTIVE_THEN_BOOT_OTHER,
    RECOVERY_ENTER_RECOVERY_MODE,
} recovery_action_t;

typedef struct
{
    recovery_action_t action;

    /**
     * Слот, который нужно стереть перед прыжком. Значим только при
     * action == RECOVERY_ERASE_ACTIVE_THEN_BOOT_OTHER.
     */
    update_policy_slot_t active_slot;
} recovery_decision_t;

/**
 * @brief Решить, что делать с текущей попыткой загрузки — таксономия
 *        отказов Фазы 6 (классы B/C/D; класс A закрывается штатным revert
 *        MCUboot без участия этой функции).
 *
 * Правила (приоритет сверху вниз):
 *   - btn2_held → RECOVERY_ENTER_RECOVERY_MODE. Ручной триггер главнее
 *     счётчика — оператор может войти в recovery в любой момент, независимо
 *     от истории попыток.
 *   - attempt_count < threshold → RECOVERY_NORMAL_BOOT. Обычная загрузка,
 *     ничего не предпринимаем (счётчик инкрементирует вызывающий код перед
 *     прыжком).
 *   - attempt_count >= threshold и есть "активный" слот (валидный, более
 *     высокой версии — тот же приём, что update_policy_decide()) — он и
 *     есть подозреваемый в зависании:
 *       - другой слот валиден (есть фолбэк) → RECOVERY_ERASE_ACTIVE_THEN_
 *         BOOT_OTHER(active_slot). Вызывающий код стирает active_slot и
 *         обнуляет счётчик — boot_go() сам выберет оставшийся слот.
 *       - другого валидного слота нет (стирать нечего — иначе ноль рабочих
 *         слотов) → RECOVERY_ENTER_RECOVERY_MODE.
 *   - attempt_count >= threshold и активного слота нет вообще →
 *     RECOVERY_ENTER_RECOVERY_MODE (нечего анализировать, нечего стирать).
 *
 * @param[in] attempt_count  Текущее значение счётчика попыток (bsp_boot_attempt_count()).
 * @param[in] threshold      Порог срабатывания фолбэка/recovery (см. RECOVERY_DEFAULT_THRESHOLD).
 * @param[in] p_slot_a       Состояние Slot A.
 * @param[in] p_slot_b       Состояние Slot Б.
 * @param[in] btn2_held      BSP_BUTTON_2 удержана на старте — ручной вход в recovery.
 */
recovery_decision_t recovery_decide(uint32_t attempt_count, uint32_t threshold,
                                    const update_policy_slot_state_t *p_slot_a,
                                    const update_policy_slot_state_t *p_slot_b, bool btn2_held);

/** @brief Дефолтный порог — 3 сброса подряд до фолбэка/recovery. */
#define RECOVERY_DEFAULT_THRESHOLD 3U

#endif /* RECOVERY_H_ */
