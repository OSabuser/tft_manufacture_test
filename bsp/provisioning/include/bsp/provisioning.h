/**
 * @file  bsp/provisioning/include/bsp/provisioning.h
 * @brief BSP Provisioning — чтение уникального идентификатора чипа (OCOTP UID).
 *
 * Предоставляет единственный примитив: чтение 8-байтового UID из OCOTP_CFG0/CFG1.
 * Не выполняет записи в OTP-ячейки и не управляет Flash.
 *
 * Аппаратура: MIMXRT1052RM §46 "On-Chip OTP Controller (OCOTP_CTRL)".
 * Shadow registers загружаются из eFuse-массива при сбросе — чтение немедленное,
 * fuse-programming не требуется.
 */

#ifndef BSP_PROVISIONING_H_
#define BSP_PROVISIONING_H_

#include "bsp/status.h"

#include <stddef.h>
#include <stdint.h>

/** @brief Длина UID в байтах (OCOTP_CFG0 + OCOTP_CFG1). */
#define BSP_PROV_UID_LEN 8U

/**
 * @brief Прочитать уникальный идентификатор чипа из OCOTP.
 *
 * Читает OCOTP_CFG0 (UID[31:0]) и OCOTP_CFG1 (UID[63:32]).
 * Результат записывается в нативном порядке байт (little-endian на Cortex-M7):
 * p_uid[0..3] = CFG0 (UID[31:0]), p_uid[4..7] = CFG1 (UID[63:32]).
 *
 * Функция выполняет OCOTP_Init() и включает clock gate перед чтением.
 * Clock gate остаётся открытым после вызова (паттерн проекта).
 *
 * @param[out] p_uid  Буфер для UID. Должен быть не менее BSP_PROV_UID_LEN байт.
 * @param[in]  len    Размер буфера в байтах. Должен быть >= BSP_PROV_UID_LEN.
 * @return BSP_OK при успехе, BSP_ERR если p_uid == NULL или len < BSP_PROV_UID_LEN.
 */
bsp_status_t bsp_prov_read_uid(uint8_t *p_uid, size_t len);

#endif /* BSP_PROVISIONING_H_ */