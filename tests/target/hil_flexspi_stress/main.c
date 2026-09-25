/*
 * hil_flexspi_stress — HIL-прошивка стресс-теста чтения QSPI (PLAN.md, фаза 0.2).
 *
 * Запуск из RAM (код в ITCM): FlexSPI не используется для исполнения, поэтому
 * его можно перенастраивать на лету — источник строба чтения (RXCLKSRC),
 * частоту и задержку выборки (DLLACR.OVRDVAL).
 *
 * Что проверяется: чтение Quad I/O (0xEB, 6 dummy — как LUT в FCB
 * tools/host/dcd/w25q128_fdcb.bin) на разных частотах при стробе через пад
 * SD_B1_05 (RXCLKSRC=1, как в FCB) и внутренней петле (RXCLKSRC=0). На V3 к
 * SD_B1_05 подключена висящая цепь LCD_DCDC_G (BOARD_DIFF.md §4.2).
 *
 * Область: последний 1 МБ 16-МБ флеш (0x00F00000). Туда один раз пишется
 * PRNG-паттерн (PREP, на безопасной частоте), дальше только чтение. На
 * dev-плате с ФС ассетов этот 1 МБ будет затёрт (BOOTLOADER_FLASH_MAP.md).
 *
 * Аппаратный сторож V3 кормится из хука SysTick, как в hil_sdram_stress.
 *
 * Команды (UART 115200, строка \r\n → одна строка ответа, кроме RUN/PREP):
 *   PING                     → PONG
 *   CFG <src> <мгц> [dll]    → CFG OK src=.. root_khz=.. dll=.. mcr0=.. dllacr=.. id=0x......
 *                              src: 0 — внутренняя петля, 1 — петля через DQS-пад;
 *                              мгц: 30|60|80|99|120|133; dll: OVRDVAL 0…63 (0 — как SDK/RM)
 *   PREP <seed>              → PREP OK written=<0|1> errors=0 ms=.. | PREP ERR <причина>
 *                              паттерн уже на месте — не пишет (флеш не изнашивается)
 *   CACHE <ON|OFF>           → OK; ON — флеш Normal WB (кэш-линии по 32 байта), OFF — Device
 *   RUN AHB <проходы> <seed> ┐ во время работы — «P» раз в ~0,5 с (keepalive),
 *   RUN IP <проходы> <seed>  ┘ в конце — RESULT test=.. passes=.. errors=.. first=0x.. exp=0x..
 *                              act=0x.. diff=0x.. io=0x. ms=..
 *   REGS                     → REGS <регистры FlexSPI/клоков>
 */

#include "board.h"
#include "fsl_clock.h"
#include "fsl_device_registers.h"
#include "fsl_flexspi.h"
#include "fsl_iomuxc.h"

#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"

#include "qspi_check.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLI_BAUD_RATE   115200U
#define CLI_LINE_MAX    96U
#define READY_PERIOD_MS 200U
#define KEEPALIVE_MS    500U
#define WDT_HALF_MS     250U /* LED_BLNK: 250 LOW / 250 HIGH */

#define FLASH_AHB_BASE    0x60000000UL
#define FLASH_MIN_BYTES   0x01000000UL /* W25Q128 */
#define REGION_OFFSET     0x00F00000UL /* последний 1 МБ */
#define REGION_BYTES      0x00100000UL
#define REGION_WORDS      (REGION_BYTES / 4U)
#define BLOCK_BYTES       0x10000UL /* erase 64 КБ */
#define PAGE_BYTES        256U
#define IP_CHUNK_BYTES    4096U
#define VERIFY_CHUNK_WORDS (IP_CHUNK_BYTES / 4U)
#define MPU_REGION_FLASH  3U /* как в board_mpu_init(): Normal WB RO, 64 МБ */

#define JEDEC_WINBOND   0xEFU
#define JEDEC_CAP_16MB  0x18U
#define SR1_BUSY        0x01U
#define SR2_QE          0x02U
#define ERASE_TIMEOUT_MS 3000U /* W25Q128JV tBE2 max 2 с */
#define PROG_TIMEOUT_MS  10U   /* tPP max 3 мс */
#define SR_TIMEOUT_MS    50U   /* tW max 15 мс */

#define PAD_CFG_FLEXSPI 0x10F1U /* как пример SDK: SRE fast, DSE R0/6, SPEED 200 МГц, keeper */
#define DLL_OVRDVAL_MAX 63U

/* ── LUT: чтение — как в FCB (0xEB, RADDR 4 линии, 6 dummy, READ 4 линии) ── */

enum
{
    LUT_READ     = 0,
    LUT_READ_SR1 = 1,
    LUT_WREN     = 2,
    LUT_ERASE64K = 3,
    LUT_PP       = 4,
    LUT_JEDEC    = 5,
    LUT_READ_SR2 = 6,
    LUT_WRITE_SR2 = 7,
    LUT_SEQ_COUNT = 8
};

#define LUT_WORDS_PER_SEQ 4U

static const uint32_t K_LUT[LUT_SEQ_COUNT * LUT_WORDS_PER_SEQ] = {
    [LUT_WORDS_PER_SEQ * LUT_READ] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0xEBU, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_4PAD, 24U),
    [LUT_WORDS_PER_SEQ * LUT_READ + 1U] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_4PAD, 6U, kFLEXSPI_Command_READ_SDR, kFLEXSPI_4PAD, 4U),
    [LUT_WORDS_PER_SEQ * LUT_READ_SR1] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x05U, kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 1U),
    [LUT_WORDS_PER_SEQ * LUT_WREN] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x06U, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0U),
    [LUT_WORDS_PER_SEQ * LUT_ERASE64K] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0xD8U, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 24U),
    [LUT_WORDS_PER_SEQ * LUT_PP] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x02U, kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 24U),
    [LUT_WORDS_PER_SEQ * LUT_PP + 1U] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 4U, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0U),
    [LUT_WORDS_PER_SEQ * LUT_JEDEC] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x9FU, kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 3U),
    [LUT_WORDS_PER_SEQ * LUT_READ_SR2] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x35U, kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 1U),
    [LUT_WORDS_PER_SEQ * LUT_WRITE_SR2] =
        FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x31U, kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 1U),
};

static bool s_configured;
static bool s_cached = true;
static uint32_t s_last_keepalive_ms;
static char s_out[192];
static uint32_t s_buf[IP_CHUNK_BYTES / 4U];

/* ── Аппаратный сторож V3: кормление из SysTick ────────────────────────── */

void bsp_systick_hook(void)
{
    static uint32_t s_ms;
    if (++s_ms >= WDT_HALF_MS)
    {
        s_ms = 0U;
        bsp_led_toggle(LED_HEARTBEAT);
    }
}

/* ── Вывод ─────────────────────────────────────────────────────────────── */

static void out(const char *p_str)
{
    bsp_uart_host_write_str(p_str);
}

static void keepalive(void)
{
    const uint32_t NOW = bsp_tick_get_ms();
    if ((NOW - s_last_keepalive_ms) >= KEEPALIVE_MS)
    {
        s_last_keepalive_ms = NOW;
        out("P\r\n");
    }
}

/* ── FlexSPI: IP-команды ──────────────────────────────────────────────── */

static status_t ip_cmd(uint8_t seq, uint32_t addr, flexspi_command_type_t type, uint32_t *p_data, size_t size)
{
    flexspi_transfer_t xfer = {
        .deviceAddress = addr,
        .port          = kFLEXSPI_PortA1,
        .cmdType       = type,
        .seqIndex      = seq,
        .SeqNumber     = 1U,
        .data          = p_data,
        .dataSize      = size,
    };
    return FLEXSPI_TransferBlocking(FLEXSPI, &xfer);
}

static uint32_t read_jedec(void)
{
    uint32_t id = 0U;
    if (ip_cmd(LUT_JEDEC, 0U, kFLEXSPI_Read, &id, 3U) != kStatus_Success)
    {
        return 0U;
    }
    /* байты в порядке приёма: mfr, type, capacity → 0xMMTTCC */
    return ((id & 0xFFU) << 16) | (id & 0xFF00U) | ((id >> 16) & 0xFFU);
}

static bool read_reg(uint8_t seq, uint8_t *p_val)
{
    uint32_t v = 0U;
    const bool OK = ip_cmd(seq, 0U, kFLEXSPI_Read, &v, 1U) == kStatus_Success;
    *p_val = (uint8_t) v;
    return OK;
}

static bool wait_ready(uint32_t timeout_ms)
{
    const uint32_t START = bsp_tick_get_ms();
    uint8_t sr1 = SR1_BUSY;
    do
    {
        keepalive();
        if (!read_reg(LUT_READ_SR1, &sr1))
        {
            return false;
        }
    } while (((sr1 & SR1_BUSY) != 0U) && ((bsp_tick_get_ms() - START) < timeout_ms));
    return (sr1 & SR1_BUSY) == 0U;
}

static bool write_enable(void)
{
    return ip_cmd(LUT_WREN, 0U, kFLEXSPI_Command, NULL, 0U) == kStatus_Success;
}

/** QE (SR2 бит 1) нужен для 0xEB. У W25Q128JV-IQ он уже стоит с завода. */
static bool ensure_qe(void)
{
    uint8_t sr2 = 0U;
    if (!read_reg(LUT_READ_SR2, &sr2))
    {
        return false;
    }
    if ((sr2 & SR2_QE) != 0U)
    {
        return true;
    }
    uint32_t v = (uint32_t) sr2 | SR2_QE;
    if (!write_enable() || (ip_cmd(LUT_WRITE_SR2, 0U, kFLEXSPI_Write, &v, 1U) != kStatus_Success) ||
        !wait_ready(SR_TIMEOUT_MS) || !read_reg(LUT_READ_SR2, &sr2))
    {
        return false;
    }
    return (sr2 & SR2_QE) != 0U;
}

/* ── FlexSPI: конфигурация ─────────────────────────────────────────────── */

static void pins_init(bool dqs_pad)
{
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B1_06_FLEXSPI_A_SS0_B, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B1_07_FLEXSPI_A_SCLK, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B1_08_FLEXSPI_A_DATA00, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B1_09_FLEXSPI_A_DATA1, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B1_10_FLEXSPI_A_DATA2, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B1_11_FLEXSPI_A_DATA3, 1U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B1_06_FLEXSPI_A_SS0_B, PAD_CFG_FLEXSPI);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B1_07_FLEXSPI_A_SCLK, PAD_CFG_FLEXSPI);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B1_08_FLEXSPI_A_DATA00, PAD_CFG_FLEXSPI);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B1_09_FLEXSPI_A_DATA1, PAD_CFG_FLEXSPI);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B1_10_FLEXSPI_A_DATA2, PAD_CFG_FLEXSPI);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B1_11_FLEXSPI_A_DATA3, PAD_CFG_FLEXSPI);
    if (dqs_pad)
    {
        /* Петля строба через пад: ALT1 + SION, как BootROM при FCB 0x0C = 1. */
        IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B1_05_FLEXSPI_A_DQS, 1U);
        IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B1_05_FLEXSPI_A_DQS, PAD_CFG_FLEXSPI);
    }
    else
    {
        /* Внутренняя петля: пад свободен — GPIO (на V3 это LCD_DCDC_G). */
        IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B1_05_GPIO3_IO05, 0U);
    }
}

static void flexspi_configure(bool dqs_pad, const qspi_clk_plan_t *p_plan, uint32_t dll)
{
    /* Корень такта FlexSPI меняется только при закрытом гейте (RM, CCM). */
    CLOCK_DisableClock(kCLOCK_FlexSpi);
    CLOCK_InitUsb1Pfd(kCLOCK_Pfd0, p_plan->pfd0_frac);
    CLOCK_SetMux(kCLOCK_FlexspiMux, 3U); /* PLL3 PFD0 */
    CLOCK_SetDiv(kCLOCK_FlexspiDiv, (uint32_t) p_plan->podf - 1U);

    pins_init(dqs_pad);

    flexspi_config_t config;
    FLEXSPI_GetDefaultConfig(&config);
    config.rxSampleClock =
        dqs_pad ? kFLEXSPI_ReadSampleClkLoopbackFromDqsPad : kFLEXSPI_ReadSampleClkLoopbackInternally;
    config.ahbConfig.enableAHBPrefetch    = true;
    config.ahbConfig.enableAHBBufferable  = true;
    config.ahbConfig.enableReadAddressOpt = true;
    config.ahbConfig.enableAHBCachable    = true;
    FLEXSPI_Init(FLEXSPI, &config);

    flexspi_device_config_t dev = {
        .flexspiRootClk       = qspi_clk_khz(p_plan) * 1000U,
        .flashSize            = FLASH_MIN_BYTES / 1024U,
        .CSIntervalUnit       = kFLEXSPI_CsIntervalUnit1SckCycle,
        .CSInterval           = 2U,
        .CSHoldTime           = 3U, /* как FCB 0x0D */
        .CSSetupTime          = 3U, /* как FCB 0x0E */
        .dataValidTime        = 0U,
        .columnspace          = 0U,
        .enableWordAddress    = false,
        .AWRSeqIndex          = 0U,
        .AWRSeqNumber         = 0U,
        .ARDSeqIndex          = LUT_READ,
        .ARDSeqNumber         = 1U,
        .AHBWriteWaitUnit     = kFLEXSPI_AhbWriteWaitUnit2AhbCycle,
        .AHBWriteWaitInterval = 0U,
    };
    FLEXSPI_SetFlashConfig(FLEXSPI, &dev, kFLEXSPI_PortA1);

    /* RM 27.5.14.4: для RXCLKSRC 0/1 — DLLCR = 0x100 (1 фиксированная ячейка).
     * OVRDVAL > 0 сдвигает момент выборки позже — так меряем окно. */
    FLEXSPI->DLLCR[0] = FLEXSPI_DLLCR_OVRDEN(1U) | FLEXSPI_DLLCR_OVRDVAL(dll);

    FLEXSPI_UpdateLUT(FLEXSPI, 0U, K_LUT, sizeof(K_LUT) / sizeof(K_LUT[0]));
    FLEXSPI_SoftwareReset(FLEXSPI);
    SCB_InvalidateDCache_by_Addr((void *) (FLASH_AHB_BASE + REGION_OFFSET), (int32_t) REGION_BYTES);
}

/* ── Кэш флеш ──────────────────────────────────────────────────────────── */

static void flash_cache_set(bool on)
{
    SCB_CleanInvalidateDCache();
    ARM_MPU_Disable();
    MPU->RBAR = ARM_MPU_RBAR(MPU_REGION_FLASH, FLASH_AHB_BASE);
    if (on)
    {
        MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_RO, 0U, 0U, 1U, 1U, 0U, ARM_MPU_REGION_SIZE_64MB);
    }
    else
    {
        MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_RO, 2U, 0U, 0U, 0U, 0U, ARM_MPU_REGION_SIZE_64MB); /* Device */
    }
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
    __DSB();
    __ISB();
    s_cached = on;
}

/* ── Проверки ──────────────────────────────────────────────────────────── */

static void verify_ahb_pass(uint32_t seed, qspi_result_t *p_res)
{
    const volatile uint32_t *const P_REGION = (const volatile uint32_t *) (FLASH_AHB_BASE + REGION_OFFSET);
    uint32_t state = qspi_prng_seed(seed);
    if (s_cached)
    {
        SCB_InvalidateDCache_by_Addr((void *) (FLASH_AHB_BASE + REGION_OFFSET), (int32_t) REGION_BYTES);
    }
    for (uint32_t w = 0U; w < REGION_WORDS; w += VERIFY_CHUNK_WORDS)
    {
        qspi_verify(&P_REGION[w], VERIFY_CHUNK_WORDS, &state, w * 4U, p_res);
        keepalive();
    }
}

static bool verify_ip_pass(uint32_t seed, qspi_result_t *p_res)
{
    uint32_t state = qspi_prng_seed(seed);
    for (uint32_t off = 0U; off < REGION_BYTES; off += IP_CHUNK_BYTES)
    {
        if (ip_cmd(LUT_READ, REGION_OFFSET + off, kFLEXSPI_Read, s_buf, IP_CHUNK_BYTES) != kStatus_Success)
        {
            return false;
        }
        qspi_verify(s_buf, VERIFY_CHUNK_WORDS, &state, off, p_res);
        keepalive();
    }
    return true;
}

/* ── Команды ───────────────────────────────────────────────────────────── */

/** Следующее слово строки, на месте, без кучи (strtok() в newlib-nano берёт кучу). */
static char *next_token(char **pp_cursor)
{
    char *p = *pp_cursor;
    while (*p == ' ')
    {
        p++;
    }
    if (*p == '\0')
    {
        *pp_cursor = p;
        return NULL;
    }
    char *const P_START = p;
    while ((*p != ' ') && (*p != '\0'))
    {
        p++;
    }
    if (*p == ' ')
    {
        *p++ = '\0';
    }
    *pp_cursor = p;
    return P_START;
}

static void print_regs(const char *p_prefix)
{
    (void) snprintf(s_out, sizeof(s_out),
                    "%s mcr0=0x%08lX dllacr=0x%08lX flsha1cr0=0x%08lX pfd480=0x%08lX cscmr1=0x%08lX "
                    "dqs_mux=0x%02lX\r\n",
                    p_prefix, (unsigned long) FLEXSPI->MCR0, (unsigned long) FLEXSPI->DLLCR[0],
                    (unsigned long) FLEXSPI->FLSHCR0[0], (unsigned long) CCM_ANALOG->PFD_480,
                    (unsigned long) CCM->CSCMR1,
                    (unsigned long) IOMUXC->SW_MUX_CTL_PAD[kIOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B1_05]);
    out(s_out);
}

static void cmd_cfg(char *p_args)
{
    char *p_cursor         = p_args;
    const char *const P_SRC = next_token(&p_cursor);
    const char *const P_MHZ = next_token(&p_cursor);
    const char *const P_DLL = next_token(&p_cursor);

    const qspi_clk_plan_t *const P_PLAN =
        (P_MHZ != NULL) ? qspi_clk_plan_find((uint32_t) strtoul(P_MHZ, NULL, 0)) : NULL;
    const uint32_t DLL = (P_DLL != NULL) ? (uint32_t) strtoul(P_DLL, NULL, 0) : 0U;
    if ((P_SRC == NULL) || ((P_SRC[0] != '0') && (P_SRC[0] != '1')) || (P_SRC[1] != '\0'))
    {
        out("CFG ERR src\r\n");
        return;
    }
    if (P_PLAN == NULL)
    {
        out("CFG ERR mhz\r\n");
        return;
    }
    if (DLL > DLL_OVRDVAL_MAX)
    {
        out("CFG ERR dll\r\n");
        return;
    }

    const bool DQS_PAD = P_SRC[0] == '1';
    flexspi_configure(DQS_PAD, P_PLAN, DLL);
    s_configured = true;
    (void) snprintf(s_out, sizeof(s_out),
                    "CFG OK src=%u root_khz=%lu dll=%lu mcr0=0x%08lX dllacr=0x%08lX id=0x%06lX\r\n",
                    DQS_PAD ? 1U : 0U, (unsigned long) qspi_clk_khz(P_PLAN), (unsigned long) DLL,
                    (unsigned long) FLEXSPI->MCR0, (unsigned long) FLEXSPI->DLLCR[0],
                    (unsigned long) read_jedec());
    out(s_out);
}

static void cmd_prep(char *p_args)
{
    char *p_cursor           = p_args;
    const char *const P_SEED = next_token(&p_cursor);
    const uint32_t SEED      = (P_SEED != NULL) ? (uint32_t) strtoul(P_SEED, NULL, 0) : 1U;
    qspi_result_t res;
    bool written = false;

    if (!s_configured)
    {
        out("PREP ERR not_configured\r\n");
        return;
    }
    const uint32_t ID = read_jedec();
    if ((((ID >> 16) & 0xFFU) != JEDEC_WINBOND) || ((ID & 0xFFU) < JEDEC_CAP_16MB))
    {
        (void) snprintf(s_out, sizeof(s_out), "PREP ERR id=0x%06lX\r\n", (unsigned long) ID);
        out(s_out);
        return;
    }
    if (!ensure_qe())
    {
        out("PREP ERR qe\r\n");
        return;
    }

    s_last_keepalive_ms = bsp_tick_get_ms();
    const uint32_t START = s_last_keepalive_ms;
    qspi_result_clear(&res);
    if (!verify_ip_pass(SEED, &res))
    {
        out("PREP ERR ip_read\r\n");
        return;
    }
    if (res.errors != 0U)
    {
        written = true;
        for (uint32_t off = 0U; off < REGION_BYTES; off += BLOCK_BYTES)
        {
            if (!write_enable() ||
                (ip_cmd(LUT_ERASE64K, REGION_OFFSET + off, kFLEXSPI_Command, NULL, 0U) != kStatus_Success) ||
                !wait_ready(ERASE_TIMEOUT_MS))
            {
                out("PREP ERR erase\r\n");
                return;
            }
        }
        uint32_t state = qspi_prng_seed(SEED);
        for (uint32_t off = 0U; off < REGION_BYTES; off += PAGE_BYTES)
        {
            qspi_prng_fill(s_buf, PAGE_BYTES / 4U, &state);
            if (!write_enable() ||
                (ip_cmd(LUT_PP, REGION_OFFSET + off, kFLEXSPI_Write, s_buf, PAGE_BYTES) != kStatus_Success) ||
                !wait_ready(PROG_TIMEOUT_MS))
            {
                out("PREP ERR program\r\n");
                return;
            }
        }
        qspi_result_clear(&res);
        if (!verify_ip_pass(SEED, &res))
        {
            out("PREP ERR ip_read\r\n");
            return;
        }
    }
    FLEXSPI_SoftwareReset(FLEXSPI); /* сбросить AHB-буферы после записи */
    SCB_InvalidateDCache_by_Addr((void *) (FLASH_AHB_BASE + REGION_OFFSET), (int32_t) REGION_BYTES);
    (void) snprintf(s_out, sizeof(s_out), "PREP %s written=%u errors=%lu ms=%lu\r\n",
                    (res.errors == 0U) ? "OK" : "ERR", written ? 1U : 0U, (unsigned long) res.errors,
                    (unsigned long) (bsp_tick_get_ms() - START));
    out(s_out);
}

static void cmd_run(char *p_args)
{
    char *p_cursor             = p_args;
    const char *const P_TEST   = next_token(&p_cursor);
    const char *const P_PASSES = next_token(&p_cursor);
    const char *const P_SEED   = next_token(&p_cursor);
    const uint32_t PASSES      = (P_PASSES != NULL) ? (uint32_t) strtoul(P_PASSES, NULL, 0) : 1U;
    const uint32_t SEED        = (P_SEED != NULL) ? (uint32_t) strtoul(P_SEED, NULL, 0) : 1U;
    qspi_result_t res;

    if (!s_configured)
    {
        out("RESULT ERR not_configured\r\n");
        return;
    }
    const bool AHB = (P_TEST != NULL) && (strcmp(P_TEST, "AHB") == 0);
    const bool IP  = (P_TEST != NULL) && (strcmp(P_TEST, "IP") == 0);
    if (!AHB && !IP)
    {
        out("RESULT ERR unknown_test\r\n");
        return;
    }

    qspi_result_clear(&res);
    s_last_keepalive_ms  = bsp_tick_get_ms();
    const uint32_t START = s_last_keepalive_ms;
    for (uint32_t pass = 0U; pass < PASSES; pass++)
    {
        if (AHB)
        {
            verify_ahb_pass(SEED, &res);
        }
        else if (!verify_ip_pass(SEED, &res))
        {
            out("RESULT ERR ip_read\r\n");
            return;
        }
        else
        {
            /* IP-проход выполнен */
        }
    }
    (void) snprintf(s_out, sizeof(s_out),
                    "RESULT test=%s passes=%lu cache=%u errors=%lu first=0x%08lX exp=0x%08lX act=0x%08lX "
                    "diff=0x%08lX io=0x%X ms=%lu\r\n",
                    P_TEST, (unsigned long) PASSES, s_cached ? 1U : 0U, (unsigned long) res.errors,
                    (unsigned long) res.first_offset, (unsigned long) res.expected, (unsigned long) res.actual,
                    (unsigned long) res.diff_or, (unsigned) qspi_io_mask(res.diff_or),
                    (unsigned long) (bsp_tick_get_ms() - START));
    out(s_out);
}

static void cli_process(char *p_line)
{
    if (strcmp(p_line, "PING") == 0)
    {
        out("PONG\r\n");
    }
    else if (strncmp(p_line, "CFG ", 4U) == 0)
    {
        cmd_cfg(&p_line[4]);
    }
    else if ((strcmp(p_line, "PREP") == 0) || (strncmp(p_line, "PREP ", 5U) == 0))
    {
        cmd_prep(&p_line[4]);
    }
    else if (strcmp(p_line, "CACHE ON") == 0)
    {
        flash_cache_set(true);
        out("OK\r\n");
    }
    else if (strcmp(p_line, "CACHE OFF") == 0)
    {
        flash_cache_set(false);
        out("OK\r\n");
    }
    else if (strncmp(p_line, "RUN ", 4U) == 0)
    {
        cmd_run(&p_line[4]);
    }
    else if (strcmp(p_line, "REGS") == 0)
    {
        print_regs("REGS");
    }
    else if (p_line[0] != '\0')
    {
        out("ERR_UNKNOWN\r\n");
    }
    else
    {
        /* пустая строка */
    }
}

/** Неблокирующий сбор строки. */
static bool cli_poll_line(char *p_line, size_t max_len)
{
    static size_t s_pos;

    while (bsp_uart_host_rx_available() > 0U)
    {
        const int32_t BYTE = bsp_uart_host_read_byte(0U);
        if ((BYTE < 0) || (BYTE == '\r'))
        {
            continue;
        }
        if (BYTE == '\n')
        {
            p_line[s_pos] = '\0';
            s_pos         = 0U;
            return true;
        }
        if (s_pos < (max_len - 1U))
        {
            p_line[s_pos++] = (char) BYTE;
        }
    }
    return false;
}

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    bsp_uart_host_init(CLI_BAUD_RATE);

    static char s_line[CLI_LINE_MAX];
    uint32_t last_ready_ms = 0U;
    bool host_seen         = false;

    for (;;)
    {
        const uint32_t NOW = bsp_tick_get_ms();
        if (!host_seen && ((NOW - last_ready_ms) >= READY_PERIOD_MS))
        {
            last_ready_ms = NOW;
            out("READY\r\n");
        }
        if (cli_poll_line(s_line, sizeof(s_line)))
        {
            host_seen = true;
            cli_process(s_line);
        }
    }
}
