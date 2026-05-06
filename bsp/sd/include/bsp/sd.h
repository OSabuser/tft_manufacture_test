/*
 * bsp_sd — инициализация SD host-контроллера и детект карты.
 *
 * Модуль управляет только железом (USDHC host, питание карты).
 * Монтирование файловой системы — в bsp_usd поверх этого модуля.
 */

#ifndef BSP_SD_H
#define BSP_SD_H

#include "bsp/status.h"

#include <stdbool.h>

/*
 * Инициализировать SD host-контроллер.
 * Вызывать до любых операций с картой.
 * Повторный вызов без deinit возвращает BSP_OK (идемпотентен).
 */
bsp_status_t bsp_sd_init(void);

/*
 * Деинициализировать SD host-контроллер и отключить питание карты.
 * Безопасен при вызове до init или после deinit.
 */
bsp_status_t bsp_sd_deinit(void);

/*
 * Проверить физическое наличие карты через регистр USDHC PRSSTAT.
 * Не требует предварительного вызова bsp_sd_init().
 * Включает тактирование USDHC1 на время чтения регистра.
 */
bool bsp_sd_is_inserted(void);

#endif /* BSP_SD_H */