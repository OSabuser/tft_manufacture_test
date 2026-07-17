/**
 * @file  sdram.c
 * @brief Подъём SEMC и верификация внешней SDRAM MT48LC16M16A2 (32 МБ, 16 бит).
 *
 * Два независимых куска:
 *
 *   bsp_sdram_configure() — поднимает SEMC. Побитовый порт проверенной DCD-последовательности
 *   (tools/host/dcd/dcd.bin)
 *
 *   bsp_sdram_init() — верификация уже поднятой памяти (DCD или configure).
 *   Регистры SEMC не модифицирует.
 *
 * Кэш: SDRAM настроена как Normal Write-Back cacheable (MPU Region 8).
 * Верификация требует явного cache maintenance перед readback — иначе
 * чтение попадает в кэш и физическая SDRAM не тестируется.
 */

#include "bsp/sdram.h"

#include "bsp/tick.h"
#include "fsl_clock.h"
#include "fsl_semc.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Константы верификации ─────────────────────────────────────────────── */

/** @brief Таймаут ожидания готовности SEMC контроллера, мс. */
#define SDRAM_SEMC_IDLE_TIMEOUT_MS 10U

/**
 * @brief Размер кэш-линии Cortex-M7, байт.
 *
 * SCB_CleanDCache_by_Addr / SCB_InvalidateDCache_by_Addr требуют
 * выравнивания адреса и размера по кэш-линии.
 * BSP_SDRAM_TEST_BASE_ADDR = 0x80200000 — кратен 32 байтам. ✓
 */
#define SDRAM_CACHE_LINE_BYTES 32U

/** @brief Эталонный паттерн верификации (чередующиеся биты). */
#define SDRAM_VERIFY_PATTERN_A 0xA5A5A5A5UL

/** @brief Инверсия эталонного паттерна. */
#define SDRAM_VERIFY_PATTERN_B 0x5A5A5A5AUL

/* ── Константы SEMC (значения из DCD, см. tools/host/dcd/dcd.bin) */

/** @brief PFD2 FRAC из DCD (PFD_528=0x00230000): 528 МГц ×18/35 ≈ 271.54 МГц. */
#define SDRAM_SEMC_PFD2_FRAC 35U

/**
 * @brief Индекс пина GPIO_EMC_39 (SEMC_DQS) в массивах IOMUXC.
 *
 * SW_MUX_CTL_PAD[]/SW_PAD_CTL_PAD[] начинаются с GPIO_EMC_00 (индекс 0), шаг 1.
 * DQS — единственный пин, которому DCD ставит SION (вход strobe должен быть
 * принудительно включён, чтобы SEMC читал собственный строб).
 */
#define SDRAM_EMC_PAD_FIRST 0U
#define SDRAM_EMC_PAD_LAST  41U /* GPIO_EMC_41 — последний EMC-пин */
#define SDRAM_DQS_PAD_INDEX 39U /* GPIO_EMC_39 = SEMC_DQS */

/** @brief SION (Software Input On), бит 4 SW_MUX_CTL_PAD — только для DQS. */
#define SDRAM_MUX_SION 0x00000010UL

/** @brief SW_PAD_CTL для всех EMC-пинов (DCD: 0x000110F9 на каждый). */
#define SDRAM_PAD_CTL_VALUE 0x000110F9UL

/** @brief Адрес SDRAM для IP-команд SEMC (совпадает с BR0 = базой SDRAM). */
#define SDRAM_SEMC_IPCMD_ADDR 0x80000000UL

/**
 * @brief Значение mode-register SDRAM (DCD IPTXDAT=0x33).
 *
 * M[2:0]=011 (burst length 8), M3=0 (sequential), M[6:4]=011 (CAS latency 3) —
 * согласуется с SDRAMCR0 (BL8/CL3) и даташитом MT48LC16M16A2.
 */
#define SDRAM_SEMC_MODE_REG 0x00000033UL

/*
 * AXI-QoS регистры арбитража доступа к SDRAM — NIC-301 GPV (Global
 * Programmer's View). Не влияют на корректность самой SDRAM — только на арбитраж при
 * конкуренции за шину (LCD/CPU vs SDRAM). */
#define SDRAM_QOS_LCD_READ  (*(volatile uint32_t *) 0x41044100UL)
#define SDRAM_QOS_LCD_WRITE (*(volatile uint32_t *) 0x41044104UL)
#define SDRAM_QOS_M7_READ   (*(volatile uint32_t *) 0x41442100UL)
#define SDRAM_QOS_M7_WRITE  (*(volatile uint32_t *) 0x41442104UL)

/* ── Состояние модуля ──────────────────────────────────────────────────── */

static bool g_s_initialised = false;

/**
 * @brief Поднять тактовую цепочку SEMC (порт секции «Clock Init» DCD).
 *
 */
static void sdram_configure_clock(void)
{
    /* DCD PLL_SYS=0x00002001 → Fout = 24 МГц × (20 + 2×loopDivider + num/denom)
     *                               = 24 × (20 + 2 + 0) = 528 МГц. */
    static const clock_sys_pll_config_t SYS_PLL = {
        .loopDivider = 1, .numerator = 0, .denominator = 1, .src = 0, /* 24 МГц OSC */
    };

    /* Гейтим SEMC на время переключения его источника (идиома clock_config.c). */
    CLOCK_DisableClock(kCLOCK_Semc);

    CLOCK_InitSysPll(&SYS_PLL);
    CLOCK_InitSysPfd(kCLOCK_Pfd2, SDRAM_SEMC_PFD2_FRAC);

    /* DCD CBCDR=0x00010D40 → SEMC_CLK_SEL=1 (alt), SEMC_ALT_CLK_SEL=0 (→PFD2),
     * SEMC_PODF=1 (÷2). */
    CLOCK_SetMux(kCLOCK_SemcAltMux, 0); /* alt = PLL2 PFD2 */
    CLOCK_SetMux(kCLOCK_SemcMux, 1);    /* SEMC clock = alt (PFD2), не periph_clk */
    CLOCK_SetDiv(kCLOCK_SemcDiv, kCLOCK_SemcDivBy2);

    CLOCK_EnableClock(kCLOCK_Semc);
}

/* ──  SEMC: пины ─────────────────────────────────────────────────── */

/**
 * @brief Замуксить пины GPIO_EMC на функцию SEMC + PAD-настройки (порт DCD).
 *
 */
static void sdram_configure_pins(void)
{
    for (uint32_t i = SDRAM_EMC_PAD_FIRST; i <= SDRAM_EMC_PAD_LAST; i++)
    {
        IOMUXC->SW_MUX_CTL_PAD[i] = 0UL; /* ALT0 = функция SEMC */
        IOMUXC->SW_PAD_CTL_PAD[i] = SDRAM_PAD_CTL_VALUE;
    }

    /* DQS: ALT0 + SION (DCD пишет сюда 0x10 вместо 0x00). */
    IOMUXC->SW_MUX_CTL_PAD[SDRAM_DQS_PAD_INDEX] = SDRAM_MUX_SION;
}

/* ── SEMC: регистры контроллера ─────────────────────────────────── */

/**
 * @brief Записать регистры контроллера SEMC (дословно «блок 2» DCD).
 *
 */
static void sdram_configure_controller(void)
{
    SEMC->MCR = 0x10000004UL; /* модуль вкл (MDIS=0), DQSMD=1 (DQS с пина), BTO=16 */

    SEMC->BMCR0 = 0x00000081UL; /* веса AXI-очереди A */
    SEMC->BMCR1 = 0x00000081UL; /* веса AXI-очереди B */

    /* Базовые регистры регионов SEMC. BR0 — сама SDRAM (0x80000000, 32 МБ,
     * VLD=1). BR1..BR8 — прочие регионы из проверенного DCD, переносятся как
     * есть (bootloader их не использует, но конфиг источника истины не режем). */
    SEMC->BR[0] = 0x8000001BUL;
    SEMC->BR[1] = 0x8200001BUL;
    SEMC->BR[2] = 0x8400001BUL;
    SEMC->BR[3] = 0x8600001BUL;
    SEMC->BR[4] = 0x90000021UL;
    SEMC->BR[5] = 0xA0000019UL;
    SEMC->BR[6] = 0xA8000017UL;
    SEMC->BR[7] = 0xA900001BUL;
    SEMC->BR[8] = 0x00000021UL;

    SEMC->IOCR = 0x000079A8UL; /* внутренний pinmux SEMC */

    /* Геометрия и тайминги SDRAM. SDRAMCR0: PS=16бит, BL=8, COL=9бит, CL=3 —
     * MT48LC16M16A2 (даташит: 512 колонок = 9 адресных бит). */
    SEMC->SDRAMCR0 = 0x00000F31UL;
    SEMC->SDRAMCR1 = 0x00652922UL;
    SEMC->SDRAMCR2 = 0x00020201UL;
    SEMC->SDRAMCR3 = 0x08193D0FUL; /* тайминги refresh; REN включим в конце */

    /* DBICR0/DBICR1 — DCD их пишет, хотя DBI-устройства на плате нет и
     * SEMC_ConfigureSDRAM() их не трогает. Инертны (регион DBI не включён),
     * но переносятся дословно ради полного соответствия проверенному DCD. */
    SEMC->DBICR0 = 0x00000021UL;
    SEMC->DBICR1 = 0x00888888UL;

    /* Параметры IP-команд: DATSZ=2 байта (запись mode-register 16-бит шиной). */
    SEMC->IPCR1 = 0x00000002UL;
    SEMC->IPCR2 = 0x00000000UL;
}

/* ── SEMC: командная последовательность инициализации SDRAM ──────── */

/**
 * @brief Прогнать init-последовательность SDRAM через IP-команды SEMC.
 *
 * precharge-all → 2×auto-refresh → mode-set → включение авто-refresh.
 *
 * @retval BSP_OK        все команды завершились успешно.
 * @retval BSP_ERR_INIT  IP-команда SEMC вернула ошибку.
 */
static bsp_status_t sdram_issue_init_sequence(void)
{
    const uint32_t ADDR = SDRAM_SEMC_IPCMD_ADDR;

    if (SEMC_SendIPCommand(SEMC, kSEMC_MemType_SDRAM, ADDR, (uint32_t) kSEMC_SDRAMCM_Prechargeall,
                           0, NULL) != kStatus_Success)
    {
        return BSP_ERR_INIT;
    }

    for (uint32_t i = 0U; i < 2U; i++)
    {
        if (SEMC_SendIPCommand(SEMC, kSEMC_MemType_SDRAM, ADDR,
                               (uint32_t) kSEMC_SDRAMCM_AutoRefresh, 0, NULL) != kStatus_Success)
        {
            return BSP_ERR_INIT;
        }
    }

    if (SEMC_SendIPCommand(SEMC, kSEMC_MemType_SDRAM, ADDR, (uint32_t) kSEMC_SDRAMCM_Modeset,
                           SDRAM_SEMC_MODE_REG, NULL) != kStatus_Success)
    {
        return BSP_ERR_INIT;
    }

    /* Включить авто-refresh + перейти на рабочие параметры refresh. DCD пишет
     * SDRAMCR3 повторно другим значением (не только бит REN) — переносим как
     * есть; это финальный «operational» refresh-конфиг после инициализации. */
    SEMC->SDRAMCR3 = 0x50210A09UL;

    return BSP_OK;
}

/* ── SEMC: AXI-QoS арбитраж ─ */

/**
 * @brief Настроить приоритеты доступа мастеров к SDRAM 
 *
 * Требуют board_mpu_init() Region 11
 */
static void sdram_configure_axi_qos(void)
{
    SDRAM_QOS_LCD_READ  = 6UL;
    SDRAM_QOS_LCD_WRITE = 6UL;
    SDRAM_QOS_M7_READ   = 7UL;
    SDRAM_QOS_M7_WRITE  = 7UL;
}

/* ── Внутренние функции верификации ────────────────────────────────────── */

/**
 * @brief Дождаться перехода SEMC в состояние IDLE.
 *
 * @return BSP_OK при успехе, BSP_ERR_TIMEOUT если SEMC не ответил.
 */
static bsp_status_t wait_semc_idle(void)
{
    const uint32_t START_MS = bsp_tick_get_ms();

    while ((SEMC->STS0 & SEMC_STS0_IDLE_MASK) == 0U)
    {
        if ((bsp_tick_get_ms() - START_MS) >= SDRAM_SEMC_IDLE_TIMEOUT_MS)
        {
            return BSP_ERR_TIMEOUT;
        }
    }

    return BSP_OK;
}

/**
 * @brief Сбросить кэш-линию по адресу тестового региона.
 *
 * Clean (запись dirty линии в SDRAM) + Invalidate (следующее чтение
 * пойдёт в SDRAM, не в кэш) + DSB (барьер завершения операции).
 */
static void flush_cache_at_test_base(void)
{
    uint32_t *const P_ADDR = (uint32_t *) BSP_SDRAM_TEST_BASE_ADDR;

    SCB_CleanDCache_by_Addr(P_ADDR, (int32_t) SDRAM_CACHE_LINE_BYTES);
    SCB_InvalidateDCache_by_Addr(P_ADDR, (int32_t) SDRAM_CACHE_LINE_BYTES);
    __DSB();
}

/**
 * @brief Записать слово в тестовый адрес, сбросить кэш, прочитать обратно.
 *
 * @param[in]  pattern   Значение для записи и верификации.
 * @return BSP_OK если readback совпал, BSP_ERR_INIT при расхождении.
 */
static bsp_status_t verify_word(uint32_t pattern)
{
    volatile uint32_t *const P_TEST = (volatile uint32_t *) BSP_SDRAM_TEST_BASE_ADDR;

    *P_TEST = pattern;
    flush_cache_at_test_base();

    return (*P_TEST == pattern) ? BSP_OK : BSP_ERR_INIT;
}

/* ── Public API ────────────────────────────────────────────────────────── */

bsp_status_t bsp_sdram_configure(void)
{
    sdram_configure_clock();
    sdram_configure_pins();
    sdram_configure_controller();

    bsp_status_t status = sdram_issue_init_sequence();
    if (status != BSP_OK)
    {
        return status;
    }
#if defined(__NIC301_EXPERIMENTS_)
    sdram_configure_axi_qos();
#endif
    return BSP_OK;
}

bsp_status_t bsp_sdram_init(void)
{
    bsp_status_t status = wait_semc_idle();
    if (status != BSP_OK)
    {
        return status;
    }

    status = verify_word(SDRAM_VERIFY_PATTERN_A);
    if (status != BSP_OK)
    {
        return BSP_ERR_INIT;
    }

    status = verify_word(SDRAM_VERIFY_PATTERN_B);
    if (status != BSP_OK)
    {
        return BSP_ERR_INIT;
    }

    g_s_initialised = true;
    return BSP_OK;
}
