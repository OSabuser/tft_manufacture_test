/*
 * sdmmc_config.c — board-level реализация SDMMC для MIMXRT1052CVJ5B.
 */

#define SD_ENABLED /* нужен для fsl_sd.h, до включений */

#include "sdmmc_config.h"

#include "fsl_iomuxc.h"

/* ---------------------------------------------------------------------------
 * Статические объекты хоста (не экспортируются)
 * ------------------------------------------------------------------------- */

/* DMA-дескрипторы: некэшируемая секция, выравнивание по требованию USDHC. */
AT_NONCACHEABLE_SECTION_ALIGN(
    static uint32_t s_dma_buf[BOARD_SDMMC_HOST_DMA_DESCRIPTOR_BUFFER_SIZE],
    SDMMCHOST_DMA_DESCRIPTOR_BUFFER_ALIGN_SIZE);

static sdmmchost_t s_host;
static sd_detect_card_t s_cd;
static sd_io_voltage_t s_io_voltage = {
    .type = BOARD_SDMMC_SD_IO_VOLTAGE_TYPE,
    .func = NULL, /* управление через регистры USDHC, не через внешний LDO */
};

/* Debug markers for runtime verification in debugger. */
volatile uint32_t g_sdmmc_dbg_dma_buf_addr         = 0U;
volatile uint32_t g_sdmmc_dbg_usdhc1_src_clock_hz  = 0U;

/*
 * Детект карты через USDHC PRES_STATE.CINST — ЕДИНЫЙ механизм и для нашего
 * гейта bsp_sd_is_inserted() (bsp/sd/src/sd.c), и для внутреннего
 * SD_PollingCardInsert() SDK (тот зовёт этот callback при kSD_DetectCardByGpioCD).
 * Не GPIO_PinRead — исторически детект через GPIO2/28 давал ложный "card
 * present" на пустом слоте (Фаза 3, симптом 1; DEBUG_LOG_PHASE3_SD.md, раунд 3).
 * Работает, пока пин D13 замаплен на USDHC1_CD_B (см. BOARD_SD_Config ниже:
 * прежний remux на GPIO2_IO28 убран, пин остаётся на USDHC1_CD_B постоянно —
 * консолидация, item 1). Тактирование USDHC1 включается идемпотентно на случай
 * вызова до полного SD_HostInit().
 *
 * Тот же PRSSTAT-бит читает и штатный host-CD путь SDK
 * (SDMMCHOST_CardDetectStatus), но kSD_DetectCardByHostCD дополнительно взводит
 * USDHC card-detect ПРЕРЫВАНИЯ — не нужны загрузчику (лишний источник IRQ перед
 * прыжком), поэтому оставляем polling через callback (kSD_DetectCardByGpioCD).
 */
static bool sd_card_detect_prsstat(void)
{
    CLOCK_EnableClock(kCLOCK_Usdhc1);
    return (USDHC_GetPresentStatusFlags(BOARD_SDMMC_SD_HOST_BASEADDR) &
            (uint32_t) kUSDHC_CardInsertedFlag) != 0U;
}

/* ---------------------------------------------------------------------------
 * Внутренние функции
 * ------------------------------------------------------------------------- */

/*
 * Возвращает частоту источника USDHC1.
 *
 * BOARD_BootClockRUN() уже настроил SysPll и PFD0 → не трогаем PLL.
 * Просто возвращаем известное значение из clock_config.h.
 */
static uint32_t get_usdhc1_src_clock_hz(void)
{
    return BOARD_BOOTCLOCKRUN_USDHC1_CLK_ROOT;
}

/*
 * Управление питанием карты: GPIO1[19] (SdPwr). Регистрируется как
 * usrParam.pwr — SDK дёргает её из SD_SetCardPower().
 */
static void sd_power_control(bool enable)
{
#if BOARD_SDMMC_SD_PWR_ACTIVE_HIGH
    GPIO_PinWrite(BOARD_SDMMC_SD_PWR_GPIO_BASE, BOARD_SDMMC_SD_PWR_GPIO_PIN, enable ? 1U : 0U);
#else
    GPIO_PinWrite(BOARD_SDMMC_SD_PWR_GPIO_BASE, BOARD_SDMMC_SD_PWR_GPIO_PIN, enable ? 0U : 1U);
#endif
}

/*
 * Инициализация GPIO питания карты как выход, начальное состояние — выкл.
 * Вызывается однократно из BOARD_SD_Config().
 */
static void sd_power_init(void)
{
    const gpio_pin_config_t cfg = {
        .direction     = kGPIO_DigitalOutput,
#if BOARD_SDMMC_SD_PWR_ACTIVE_HIGH
        .outputLogic   = 0U, /* питание выключено при старте */
#else
        .outputLogic   = 1U, /* питание выключено при старте */
#endif
        .interruptMode = kGPIO_NoIntmode,
    };
    GPIO_PinInit(BOARD_SDMMC_SD_PWR_GPIO_BASE, BOARD_SDMMC_SD_PWR_GPIO_PIN, &cfg);
}

/*
 * Динамическая настройка пад-конфигурации линий SD в зависимости от частоты.
 * Вызывается SDMMC стеком при смене скорости (HS, SDR50, SDR104).
 *
 * speed/strength подобраны по таблице EVK — для данной платы проверить
 * при трассировке > 50 MHz.
 */
static void sd_pin_config(uint32_t freq)
{
    uint32_t speed;
    uint32_t strength;

    if (freq <= 50000000U)
    {
        speed    = 0U;
        strength = 7U;
    }
    else if (freq <= 100000000U)
    {
        speed    = 2U;
        strength = 7U;
    }
    else
    {
        speed    = 3U;
        strength = 7U;
    }

    const uint32_t pad = IOMUXC_SW_PAD_CTL_PAD_SPEED(speed) | IOMUXC_SW_PAD_CTL_PAD_SRE_MASK |
                         IOMUXC_SW_PAD_CTL_PAD_PKE_MASK | IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                         IOMUXC_SW_PAD_CTL_PAD_HYS_MASK |
                         IOMUXC_SW_PAD_CTL_PAD_PUS(1) /* 47k pull-up */
                         | IOMUXC_SW_PAD_CTL_PAD_DSE(strength);

    const uint32_t clk_pad = IOMUXC_SW_PAD_CTL_PAD_SPEED(speed) | IOMUXC_SW_PAD_CTL_PAD_SRE_MASK |
                             IOMUXC_SW_PAD_CTL_PAD_HYS_MASK |
                             IOMUXC_SW_PAD_CTL_PAD_PUS(0) /* no pull on CLK */
                             | IOMUXC_SW_PAD_CTL_PAD_DSE(strength);

    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_00_USDHC1_CMD, pad);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_01_USDHC1_CLK, clk_pad);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_02_USDHC1_DATA0, pad);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_03_USDHC1_DATA1, pad);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_04_USDHC1_DATA2, pad);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_05_USDHC1_DATA3, pad);
    /*
     * CD (D13) здесь НЕ конфигурируем: пин на USDHC1_CD_B (см. BOARD_SD_Config),
     * pad задан в BOARD_InitPins() и на железе даёт корректный CINST. Прежняя
     * настройка pad'а GPIO2_IO28 убрана вместе с GPIO-детектом (item 1,
     * DEBUG_LOG_PHASE3_SD.md).
     */
}

/* ---------------------------------------------------------------------------
 * Публичный API
 * ------------------------------------------------------------------------- */

#ifdef SD_ENABLED
void BOARD_SD_Config(void *card, sd_cd_t cd, uint32_t host_irq_priority, void *user_data)
{
    assert(card != NULL);

    /* --- host --- */
    s_host.dmaDesBuffer         = s_dma_buf;
    s_host.dmaDesBufferWordsNum = BOARD_SDMMC_HOST_DMA_DESCRIPTOR_BUFFER_SIZE;
    s_host.enableCacheControl   = BOARD_SDMMC_HOST_CACHE_CONTROL;

    sd_card_t *sd = (sd_card_t *) card;

    sd->host                                = &s_host;
    sd->host->hostController.base           = BOARD_SDMMC_SD_HOST_BASEADDR;
    sd->host->hostController.sourceClock_Hz = get_usdhc1_src_clock_hz();
    g_sdmmc_dbg_dma_buf_addr                = (uint32_t)(uintptr_t)s_dma_buf;
    g_sdmmc_dbg_usdhc1_src_clock_hz         = sd->host->hostController.sourceClock_Hz;

    /* --- card detect: USDHC PRES_STATE.CINST через callback (polling, без IRQ) --- */
    s_cd.cdDebounce_ms = BOARD_SDMMC_SD_CD_DEBOUNCE_MS;
    s_cd.type          = BOARD_SDMMC_SD_CD_TYPE; /* kSD_DetectCardByGpioCD → callback ниже */
    s_cd.cardDetected  = sd_card_detect_prsstat;
    s_cd.callback      = cd;   /* обычно NULL из bsp_sd */
    s_cd.userData      = user_data;

    sd->usrParam.cd         = &s_cd;
    sd->usrParam.pwr        = sd_power_control;
    sd->usrParam.ioStrength = sd_pin_config;
    sd->usrParam.ioVoltage  = &s_io_voltage;
    sd->usrParam.maxFreq    = BOARD_SDMMC_SD_HOST_SUPPORT_SDR104_FREQ;

    /* --- GPIO питания --- */
    sd_power_init();
    /*
     * CD_B (GPIO_B1_12 / physical D13) остаётся на USDHC1_CD_B постоянно —
     * единый механизм детекта через PRES_STATE.CINST (sd_card_detect_prsstat
     * выше + гейт bsp_sd_is_inserted). Прежней двойной маршрутизации
     * (remux на GPIO2_IO28 для GPIO-чтения внутри f_mount) больше нет —
     * см. DEBUG_LOG_PHASE3_SD.md, раунд 3 «консолидация детекта» (item 1);
     * она убирала латентную хрупкость: после первого bsp_sd_init() пин уходил
     * на GPIO2_IO28 и повторный PRSSTAT-скан ослеп бы. Явно переустанавливаем
     * альт-функцию (BOARD_InitPins() её тоже ставит — так модуль не зависит от
     * порядка инициализации). Pad этого пина оставляем как задал BOARD_InitPins:
     * на железе CINST на нём читается корректно (Фаза 3, все сценарии).
     */
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_12_USDHC1_CD_B, 0U);
    /* Pad-конфиг линий SD (CMD/CLK/DATA) сразу для ранних CMD (CMD0/CMD8/CMD55/ACMD41). */
    sd_pin_config(400000U);

    /* --- приоритет прерывания хоста --- */
    NVIC_SetPriority(BOARD_SDMMC_SD_HOST_IRQ, host_irq_priority);
}
#endif /* SD_ENABLED */
