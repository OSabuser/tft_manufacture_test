#include "board.h"

#include "clock_config.h"
#include "core_cm7.h"
#include "fsl_common.h"
#include "pin_mux.h"

/* Non-cacheable region boundaries — exported by linker script */
extern uint32_t __NCACHE_REGION_START[];
extern uint32_t __NCACHE_REGION_SIZE[];

/*! @brief Конфигурация MPU: регионы памяти, атрибуты кэширования.
 *
 *  Должна вызываться до включения D-Cache/I-Cache.
 *  Конкретный набор регионов определяется макросами времени сборки:
 *  - XIP_EXTERNAL_FLASH — добавляет cacheable RO регион для FlexSPI NOR
 *  - BOARD_MPU_SDRAM   — добавляет cacheable RW регион для SDRAM 32 MB
 *
 *  После возврата MPU включён, D-Cache и I-Cache включены.
 */
static void board_mpu_init(void)
{
    uint32_t nc_start   = (uint32_t) __NCACHE_REGION_START;
    uint32_t nc_size    = (uint32_t) __NCACHE_REGION_SIZE;
    volatile uint32_t i = 0U;

    if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U)
    {
        SCB_DisableICache();
    }
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_DisableDCache();
    }

    ARM_MPU_Disable();

    /* Region 0: deny-all, Strongly Ordered — errata 1013783-B workaround. */
    MPU->RBAR = ARM_MPU_RBAR(0U, 0x00000000U);
    MPU->RASR = ARM_MPU_RASR(1U, ARM_MPU_AP_NONE, 0U, 0U, 0U, 0U, 0U, ARM_MPU_REGION_SIZE_4GB);

    /* Region 1: Device, 0x80000000, 512 MB. */
    MPU->RBAR = ARM_MPU_RBAR(1U, 0x80000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 2U, 0U, 0U, 0U, 0U, ARM_MPU_REGION_SIZE_512MB);

    /* Region 2: Device, 0x60000000, 512 MB. */
    MPU->RBAR = ARM_MPU_RBAR(2U, 0x60000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 2U, 0U, 0U, 0U, 0U, ARM_MPU_REGION_SIZE_512MB);

#if defined(XIP_EXTERNAL_FLASH) && (XIP_EXTERNAL_FLASH == 1)
    /* Region 3: Normal WB, RO, XIP — overrides Region 2 for Flash 64 MB. */
    MPU->RBAR = ARM_MPU_RBAR(3U, 0x60000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_RO, 0U, 0U, 1U, 1U, 0U, ARM_MPU_REGION_SIZE_64MB);
#endif

    /* Region 4: Device, 0x00000000, 1 GB. */
    MPU->RBAR = ARM_MPU_RBAR(4U, 0x00000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 2U, 0U, 0U, 0U, 0U, ARM_MPU_REGION_SIZE_1GB);

    /* Region 5: Normal WB, ITCM 128 KB — overrides Region 4. */
    MPU->RBAR = ARM_MPU_RBAR(5U, 0x00000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 0U, 0U, 1U, 1U, 0U, ARM_MPU_REGION_SIZE_128KB);

    /* Region 6: Normal WB, DTCM 128 KB. */
    MPU->RBAR = ARM_MPU_RBAR(6U, 0x20000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 0U, 0U, 1U, 1U, 0U, ARM_MPU_REGION_SIZE_128KB);

    /* Region 7: Normal WB, OCRAM 256 KB. */
    MPU->RBAR = ARM_MPU_RBAR(7U, 0x20200000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 0U, 0U, 1U, 1U, 0U, ARM_MPU_REGION_SIZE_256KB);

#if defined(BOARD_MPU_SDRAM) && (BOARD_MPU_SDRAM == 1)
    /* Region 8: Normal WB, SDRAM 32 MB — overrides Region 1. */
    MPU->RBAR = ARM_MPU_RBAR(8U, 0x80000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 0U, 0U, 1U, 1U, 0U, ARM_MPU_REGION_SIZE_32MB);
#endif

    /* Region 9: Non-cacheable OCRAM — overrides Region 7 for USB DMA. */
    while ((nc_size >> i) > 1U)
    {
        i++;
    }

    if (i != 0U)
    {
        MPU->RBAR = ARM_MPU_RBAR(9U, nc_start);
        MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 1U, 0U, 0U, 0U, 0U, i - 1U);
    }

    /* Region 10: Device, peripherals 0x40000000, 4 MB. */
    MPU->RBAR = ARM_MPU_RBAR(10U, 0x40000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 2U, 0U, 0U, 0U, 0U, ARM_MPU_REGION_SIZE_4MB);

    /* Region 11: Device, NIC-301 GPV (bus-arbitration QoS) 0x41000000, 8 MB.
     * Region 10 не покрывает — GPV0/SIM_MAIN (0x41000000) и GPV4/SIM_M7
     * (0x41400000, "Cortex-M7 read/write_qos") лежат за пределами его 4 MB
     * от 0x40000000, попадают только под Region 0 (deny-all, errata-воркэраунд
     * выше) → запись фолтит, хотя регистры реальные и документированы (i.MX
     * RT1050 RM, гл. 29 "Network Interconnect Bus System (NIC-301)"). Нужен
     * bsp_sdram_configure() для read_qos/write_qos регионов SDRAM/LCD/M7. */
    MPU->RBAR = ARM_MPU_RBAR(11U, 0x41000000U);
    MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 2U, 0U, 0U, 0U, 0U, ARM_MPU_REGION_SIZE_8MB);

    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);

    SCB_EnableDCache();
    SCB_EnableICache();
}

static void board_dwt_init(void)
{
    /* DWT cycle counter — нужен для точного SDK_DelayAtLeastUs.
     * Без этого SDK использует software loop с непредсказуемым timing. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void board_hw_init(void)
{
    board_dwt_init();
    BOARD_InitPins();
    BOARD_BootClockRUN();
    board_mpu_init();
}
