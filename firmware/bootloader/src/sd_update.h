/**
 * @file  sd_update.h
 * @brief Оркестрация установки образа tft_app с microSD в неактивный слот.
 *
 * См. firmware/bootloader/PLAN.md, Фаза 3.
 */

#ifndef SD_UPDATE_H_
#define SD_UPDATE_H_

#include <stdbool.h>

/**
 * @brief Одна попытка: смонтировать SD, найти TFT_APP.BIN, при необходимости
 *        установить его в неактивный слот.
 *
 * Пишет только в неактивный слот (см. update_policy.h) — уже выбранный/
 * загружаемый слот никогда не трогается. Сам не вызывает boot_go()/
 * boot_select_and_jump() — решение "когда прыгать" остаётся за main.c,
 * которое обязано вызвать его ровно один раз за сессию питания (см.
 * slot_version.h о том, почему boot_go() нельзя звать повторно).
 *
 * Ничего не делает, если SD не вставлена (bsp_sd_is_inserted() == false) —
 * безопасно вызывать многократно, в т.ч. из цикла ожидания в main.c.
 *
 * @param downgrade_button_held  Состояние BSP_BUTTON_1, сэмплированное ОДИН РАЗ
 *        при старте (main.c, до медленной SD-инициализации) и защёлкнутое.
 *        true → разрешён форс. даунгрейд более старого подписанного образа
 *        (см. update_policy.h). Передаётся, а не читается здесь, чтобы жест
 *        "удержание при включении" ловился в предсказуемый ранний момент, а не
 *        через несколько секунд внутри run_update() (DEBUG_LOG_PHASE3_SD.md).
 *
 * @pre bsp_qspi_init() уже вызван.
 */
void sd_update_check(bool downgrade_button_held);

#endif /* SD_UPDATE_H_ */
