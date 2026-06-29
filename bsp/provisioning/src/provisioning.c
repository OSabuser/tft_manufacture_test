/**
 * @file  bsp/provisioning/src/bsp_provisioning.c
 * @brief Реализация bsp_provisioning: чтение UID из OCOTP_CFG0/CFG1.
 *
 * На i.MX RT1052 FSL_FEATURE_OCOTP_HAS_TIMING_CTRL == 0, поэтому
 * OCOTP_Init(OCOTP, 0U) вызывается без частоты (частота не используется драйвером).
 *
 * Shadow registers загружаются из eFuse при сбросе (MIMXRT1052RM §46.3.1).
 * OCOTP_ReadFuseShadowRegister() читает уже готовые значения без fuse programming.
 *
 * Индексы shadow registers (MIMXRT1052RM Table 46-2):
 *   Bank 0, Word 1 (index 1) → OCOTP_CFG0 → UID[31:0]
 *   Bank 0, Word 2 (index 2) → OCOTP_CFG1 → UID[63:32]
 *   index = bank * 4 + word
 */

#include "bsp/provisioning.h"

#include "fsl_clock.h"
#include "fsl_common.h"
/* --------------------------------------------------------------------------
 * Константы
 * ----------------------------------------------------------------------- */

/** @brief Shadow register index для OCOTP_CFG0 (Bank 0, Word 1 → UID[31:0]). */
#define PROV_OCOTP_IDX_CFG0 1U

/** @brief Shadow register index для OCOTP_CFG1 (Bank 0, Word 2 → UID[63:32]). */
#define PROV_OCOTP_IDX_CFG1 2U

/* --------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

bsp_status_t bsp_prov_read_uid(uint8_t *p_uid, size_t len)
{
    if ((p_uid == NULL) || (len < BSP_PROV_UID_LEN))
    {
        return BSP_ERR_PARAM;
    }

    /*
     * Shadow registers загружаются из eFuse при сбросе и доступны напрямую
     * через memory-mapped адреса без инициализации контроллера.
     * Паттерн идентичен fsl_silicon_id_soc.c из NXP SDK.
     * CFG0 = UID[31:0], CFG1 = UID[63:32].
     */
    /* NOLINTNEXTLINE(bugprone-casting-through-void) */
    *((uint32_t *) (uintptr_t) &p_uid[0U]) = OCOTP->CFG0;
    /* NOLINTNEXTLINE(bugprone-casting-through-void) */
    *((uint32_t *) (uintptr_t) &p_uid[4U]) = OCOTP->CFG1;

    return BSP_OK;
}