/**
 * @file  update_policy.h
 * @brief Чистая логика решения "устанавливать ли SD-кандидат" — без
 *        аппаратных зависимостей (flash/FatFS), полностью host-тестируема.
 *
 * См. firmware/bootloader/PLAN.md, Фаза 3.
 */

#ifndef UPDATE_POLICY_H_
#define UPDATE_POLICY_H_

#include "bootutil/image.h"

#include <stdbool.h>

/** @brief Логический слот (индекс сисфлеша, не физический адрес). */
typedef enum
{
    UPDATE_POLICY_SLOT_A = 0,
    UPDATE_POLICY_SLOT_B = 1,
} update_policy_slot_t;

/** @brief Состояние одного слота глазами вызывающего (см. slot_version.h). */
typedef struct
{
    bool valid;                   /**< Есть валидный (прошедший bootutil_img_validate) образ. */
    struct image_version version; /**< Значимо только если valid == true. */
} update_policy_slot_state_t;

typedef enum
{
    UPDATE_POLICY_SKIP = 0,
    UPDATE_POLICY_INSTALL,
} update_policy_action_t;

typedef struct
{
    update_policy_action_t action;
    update_policy_slot_t target_slot; /**< Значим только если action == UPDATE_POLICY_INSTALL. */

    /**
     * Значим только при action == UPDATE_POLICY_INSTALL. Если true —
     * вызывающий код обязан, ПОСЛЕ успешной установки и пост-записи
     * валидации target_slot, стереть слот, который был активным ДО
     * установки (см. rationale ниже про форс. даунгрейд).
     */
    bool erase_previous_active;
} update_policy_result_t;

/**
 * @brief Решить, устанавливать ли SD-кандидат, и в какой слот.
 *
 * Правила:
 *   - "Активный" слот — валидный слот с более высокой версией; если валиден
 *     только один — он активный; если ни одного — активного слота нет.
 *   - Целевой слот установки — всегда НЕ активный (активный не перезаписываем
 *     никогда, независимо от исхода сравнения версий). Если активного слота
 *     нет — по умолчанию Slot A.
 *   - Кандидат новее активного (или активного слота нет вообще) → INSTALL,
 *     erase_previous_active = false. Прежний активный слот сам проиграет
 *     сравнение версий в boot_go() — стирать его не нужно.
 *   - Кандидат старше или равен активному, кнопка не удержана → SKIP.
 *   - Кандидат старше активного, кнопка удержана → INSTALL,
 *     erase_previous_active = true. Без этого форс. даунгрейд не имел бы
 *     эффекта: прежний (более новый) активный слот остался бы валиден и
 *     снова выиграл бы сравнение версий в boot_go(), несмотря на успешную
 *     запись более старого образа в другой слот. Стирание — обязанность
 *     вызывающего кода и только ПОСЛЕ подтверждения, что только что
 *     установленный образ валиден (иначе на короткое время не осталось бы
 *     ни одного рабочего слота).
 *   - Кандидат равен активному, кнопка удержана → SKIP (не форсируем
 *     переустановку той же версии).
 *
 * @param[in] p_slot_a         Состояние Slot A.
 * @param[in] p_slot_b         Состояние Slot Б.
 * @param[in] p_candidate_ver  Версия образа-кандидата на SD.
 * @param[in] button_held      Кнопка даунгрейда (BSP_BUTTON_1) удержана на старте.
 */
update_policy_result_t update_policy_decide(const update_policy_slot_state_t *p_slot_a,
                                            const update_policy_slot_state_t *p_slot_b,
                                            const struct image_version *p_candidate_ver,
                                            bool button_held);

/**
 * @brief Сравнить версии образов: major.minor.revision, без build_num — то
 *        же соглашение, что boot_version_cmp() в sdk/.../bootutil/loader.c
 *        (static там, не экспортируется — здесь свой аналог).
 *
 * @retval <0  p_ver1 < p_ver2
 * @retval  0  p_ver1 == p_ver2
 * @retval >0  p_ver1 > p_ver2
 */
int image_version_compare(const struct image_version *p_ver1, const struct image_version *p_ver2);

#endif /* UPDATE_POLICY_H_ */
