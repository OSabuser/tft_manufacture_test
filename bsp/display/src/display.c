/**
 * @file  display.c
 * @brief BSP: ELCDIF display driver — TFT4 / TFT7 / TFT8 / TFT10.
 *
 * Пиксельный клок:
 *   TFT7  — PLL2 (mux=0), pre_div=2, div=4 → 528/3/5 = 35.2 МГц
 *   TFT8  — PLL2 (mux=0), pre_div=2, div=3 → 528/3/4 = 44.0 МГц
 *   TFT4  — Video PLL (mux=2) — TODO: требует CLOCK_InitVideoPll.
 *           Возвращает BSP_ERR_NOT_SUPPORTED до реализации.
 *
 * Управляющие GPIO (TFT7/TFT8/TFT10):
 *   LcdLed  GPIO1[20] — подсветка, active-high
 *   Lcdlr_value   GPIO1[28] — горизонтальный скан SHlr_value
 *   LcdMode GPIO1[29] — DE/SYNC mode: HIGH=DE mode (обязательно для ELCDIF)
 *   LcdUd   GPIO1[30] — вертикальный скан UPDN
 *   LcdDithb GPIO1[31] — dithering bypass: HIGH=disable (IC default)
 *
 * IOMUXC конфигурируется в BOARD_InitPins() — здесь только GPIO_PinWrite.
 *
 * ISR LCDIF_IRQHandler размещён в ITCM (AT_QUICKACCESS_SECTION_CODE).
 */

#include "bsp/display.h"

#include "fsl_clock.h"
#include "fsl_elcdif.h"
#include "fsl_gpio.h"

/* ── GPIO пины ───────────────────────────────────────────────────────── */

#define DISPLAY_BACKLIGHT_GPIO GPIO1
#define DISPLAY_BACKLIGHT_PIN  20U

#define DISPLAY_lr_value_GPIO GPIO1
#define DISPLAY_lr_value_PIN  28U
#define DISPLAY_MODE_GPIO     GPIO1
#define DISPLAY_MODE_PIN      29U
#define DISPLAY_UD_GPIO       GPIO1
#define DISPLAY_UD_PIN        30U
#define DISPLAY_DITHB_GPIO    GPIO1
#define DISPLAY_DITHB_PIN     31U

/* ── Делители пиксельного клока (PLL2 = 528 МГц) ───────────────────── */

#define DISPLAY_CLK_MUX_PLL2      0U /**< kCLOCK_LcdifPreMux = PLL2. */
#define DISPLAY_CLK_MUX_VIDEO_PLL 2U /**< kCLOCK_LcdifPreMux = Video PLL (TFT4). */

#define DISPLAY_TFT7_CLK_PREDIV 2U /**< ÷3 → 528/3 = 176 МГц. */
#define DISPLAY_TFT7_CLK_DIV    4U /**< ÷5 → 176/5 = 35.2 МГц. */
#define DISPLAY_TFT8_CLK_PREDIV 2U /**< ÷3 → 528/3 = 176 МГц. */
#define DISPLAY_TFT8_CLK_DIV    3U /**< ÷4 → 176/4 = 44.0 МГц. */

/* ── Тайминги TFT4 (480 × 272) ───────────────────────────────────────── */

#define DISPLAY_TFT4_W   480U
#define DISPLAY_TFT4_H   272U
#define DISPLAY_TFT4_HSW 41U
#define DISPLAY_TFT4_HFP 2U
#define DISPLAY_TFT4_HBP 2U
#define DISPLAY_TFT4_VSW 10U
#define DISPLAY_TFT4_VFP 2U
#define DISPLAY_TFT4_VBP 2U

/* ── Тайминги TFT7 (1024 × 600) ──────────────────────────────────────── */

#define DISPLAY_TFT7_W   1024U
#define DISPLAY_TFT7_H   600U
#define DISPLAY_TFT7_HSW 1U
#define DISPLAY_TFT7_HFP 210U
#define DISPLAY_TFT7_HBP 46U
#define DISPLAY_TFT7_VSW 1U
#define DISPLAY_TFT7_VFP 12U
#define DISPLAY_TFT7_VBP 23U

/* ── Тайминги TFT8 (800 × 600) ───────────────────────────────────────── */

#define DISPLAY_TFT8_W   800U
#define DISPLAY_TFT8_H   600U
#define DISPLAY_TFT8_HSW 1U
#define DISPLAY_TFT8_HFP 210U
#define DISPLAY_TFT8_HBP 46U
#define DISPLAY_TFT8_VSW 1U
#define DISPLAY_TFT8_VFP 12U
#define DISPLAY_TFT8_VBP 23U

/* ── Полярность (одинакова для всех трёх дисплеев) ───────────────────── */

#define DISPLAY_POL_FLAGS                                                                          \
    (kELCDIF_DataEnableActiveHigh | kELCDIF_VsyncActiveLow | kELCDIF_HsyncActiveLow |              \
     kELCDIF_DriveDataOnRisingClkEdge)

/* ── IRQ приоритет ───────────────────────────────────────────────────── */

#define DISPLAY_IRQ_PRIORITY 2U

/* ── Внутренние типы ─────────────────────────────────────────────────── */

typedef struct display_hw_cfg_s
{
    uint16_t width;
    uint16_t height;
    uint8_t hsw;
    uint8_t hfp;
    uint8_t hbp;
    uint8_t vsw;
    uint8_t vfp;
    uint8_t vbp;
    uint32_t pol_flags;
    uint8_t clk_mux;
    uint8_t clk_pre_div;
    uint8_t clk_div;
    bool has_orientation_pins; /**< false = TFT4. */
} display_hw_cfg_t;

typedef struct display_state_s
{
    bsp_display_type_t type;
    bsp_display_size_t size;
    bsp_display_frame_cb_t frame_cb;
    bool initialised;
} display_state_t;

/* ── Таблица конфигураций ─────────────────────────────────────────────── */

static const display_hw_cfg_t K_HW_CFG[BSP_DISPLAY_COUNT] = {

    [BSP_DISPLAY_TFT4] = {
        .width               = DISPLAY_TFT4_W,
        .height              = DISPLAY_TFT4_H,
        .hsw                 = DISPLAY_TFT4_HSW,
        .hfp                 = DISPLAY_TFT4_HFP,
        .hbp                 = DISPLAY_TFT4_HBP,
        .vsw                 = DISPLAY_TFT4_VSW,
        .vfp                 = DISPLAY_TFT4_VFP,
        .vbp                 = DISPLAY_TFT4_VBP,
        .pol_flags           = DISPLAY_POL_FLAGS,
        .clk_mux             = DISPLAY_CLK_MUX_VIDEO_PLL,
        .clk_pre_div         = 0U, /* placeholder — Video PLL TODO */
        .clk_div             = 0U,
        .has_orientation_pins = false,
    },

    [BSP_DISPLAY_TFT7] = {
        .width               = DISPLAY_TFT7_W,
        .height              = DISPLAY_TFT7_H,
        .hsw                 = DISPLAY_TFT7_HSW,
        .hfp                 = DISPLAY_TFT7_HFP,
        .hbp                 = DISPLAY_TFT7_HBP,
        .vsw                 = DISPLAY_TFT7_VSW,
        .vfp                 = DISPLAY_TFT7_VFP,
        .vbp                 = DISPLAY_TFT7_VBP,
        .pol_flags           = DISPLAY_POL_FLAGS,
        .clk_mux             = DISPLAY_CLK_MUX_PLL2,
        .clk_pre_div         = DISPLAY_TFT7_CLK_PREDIV,
        .clk_div             = DISPLAY_TFT7_CLK_DIV,
        .has_orientation_pins = true,
    },

    [BSP_DISPLAY_TFT8] = {
        .width               = DISPLAY_TFT8_W,
        .height              = DISPLAY_TFT8_H,
        .hsw                 = DISPLAY_TFT8_HSW,
        .hfp                 = DISPLAY_TFT8_HFP,
        .hbp                 = DISPLAY_TFT8_HBP,
        .vsw                 = DISPLAY_TFT8_VSW,
        .vfp                 = DISPLAY_TFT8_VFP,
        .vbp                 = DISPLAY_TFT8_VBP,
        .pol_flags           = DISPLAY_POL_FLAGS,
        .clk_mux             = DISPLAY_CLK_MUX_PLL2,
        .clk_pre_div         = DISPLAY_TFT8_CLK_PREDIV,
        .clk_div             = DISPLAY_TFT8_CLK_DIV,
        .has_orientation_pins = true,
    },

    /* TFT10: спецификации не определены — TODO */
    [BSP_DISPLAY_TFT10] = { 0 },
};

/* ── Состояние модуля ─────────────────────────────────────────────────── */

static display_state_t g_s_display = {
    .type        = BSP_DISPLAY_COUNT,
    .frame_cb    = NULL,
    .initialised = false,
};

/* ── ISR ─────────────────────────────────────────────────────────────── */

/*
 * ISR размещён в ITCM для минимальной задержки и исключения кэш-промахов.
 * Только вызывает зарегистрированный callback — не трогает состояние.
 */
// NOLINTNEXTLINE(readability-identifier-naming) — SDK-mandated ISR symbol
AT_QUICKACCESS_SECTION_CODE(void LCDIF_IRQHandler(void));
void LCDIF_IRQHandler(void) // NOLINT(readability-identifier-naming)
{
    uint32_t flags = ELCDIF_GetInterruptStatus(LCDIF);
    ELCDIF_ClearInterruptStatus(LCDIF, flags);

    if (((flags & (uint32_t) kELCDIF_CurFrameDone) != 0U) && (g_s_display.frame_cb != NULL))
    {
        g_s_display.frame_cb();
    }

    __DSB();
}

/* ── Вспомогательные функции ─────────────────────────────────────────── */

/**
 * @brief Настроить делители пиксельного клока.
 *
 * TFT4 требует инициализации Video PLL (TODO) — возвращает ошибку.
 * TFT7/TFT8 используют PLL2 (528 МГц), который уже инициализирован
 * в BOARD_BootClockRUN().
 */
static bsp_status_t init_pixelclock(const display_hw_cfg_t *p_cfg)
{
    if (p_cfg->clk_mux == DISPLAY_CLK_MUX_VIDEO_PLL)
    {
        /* TODO: CLOCK_InitVideoPll() для TFT4.
         * Video PLL деинициализирован в BOARD_BootClockRUN() — нужна
         * дополнительная инициализация перед использованием. */
        return BSP_ERR_NOT_SUPPORTED;
    }

    CLOCK_SetMux(kCLOCK_LcdifPreMux, p_cfg->clk_mux);
    CLOCK_SetDiv(kCLOCK_LcdifPreDiv, p_cfg->clk_pre_div);
    CLOCK_SetDiv(kCLOCK_LcdifDiv, p_cfg->clk_div);
    return BSP_OK;
}

/** @brief Включить подсветку (GPIO1[20] = HIGH). */
static void init_backlight(void)
{
    GPIO_PinWrite(DISPLAY_BACKLIGHT_GPIO, DISPLAY_BACKLIGHT_PIN, 1U);
}

/**
 * @brief Установить ножки lr_value/UD в состояние ROTATE_0 по умолчанию.
 *
 * Итоговая ориентация задаётся вызовом bsp_display_set_rotation()
 * после bsp_display_init().
 */
static void init_orientation_pins(void)
{
    GPIO_PinWrite(DISPLAY_lr_value_GPIO, DISPLAY_lr_value_PIN, 1U); /* ROTATE_0: lr_value=1 */
    GPIO_PinWrite(DISPLAY_UD_GPIO, DISPLAY_UD_PIN, 0U);             /* ROTATE_0: UD=0 */
}

/**
 * @brief Инициализировать MODE и DITHB пины контроллера HX8264-D02.
 *
 * MODE=1: DE mode (обязательно для ELCDIF, использующего DE/enable сигнал).
 * DITHB=1: отключить дизеринг (IC default согласно даташиту, "normally pull high").
 *
 * Вызывается только для дисплеев с has_orientation_pins = true.
 */
static void init_mode_dither_pins(void)
{
    GPIO_PinWrite(DISPLAY_MODE_GPIO, DISPLAY_MODE_PIN, 1U);   /* MODE=1: DE mode */
    GPIO_PinWrite(DISPLAY_DITHB_GPIO, DISPLAY_DITHB_PIN, 1U); /* DITHB=1: disable */
}

/** @brief Включить IRQ FRAME_DONE с фиксированным приоритетом. */
static void enable_lcd_interrupt(void)
{
    NVIC_SetPriority(LCDIF_IRQn, DISPLAY_IRQ_PRIORITY);
    EnableIRQ(LCDIF_IRQn);
    ELCDIF_EnableInterrupts(LCDIF, kELCDIF_CurFrameDoneInterruptEnable);
}

/** @brief Смаппить BSP-формат пикселя на формат памяти ELCDIF. */
static elcdif_pixel_format_t map_pixel_format(bsp_display_pixel_format_t format)
{
    /* dataBus остаётся 24-бит независимо: pixelFormat задаёт лишь ширину слова в
     * памяти (WORD_LENGTH), ELCDIF расширяет 565→24 на пинах (см. fsl_elcdif.c
     * s_pixelFormatReg). */
    return (format == BSP_DISPLAY_PIXEL_RGB565) ? kELCDIF_PixelFormatRGB565
                                                : kELCDIF_PixelFormatXRGB8888;
}

/** @brief Заполнить конфигурацию ELCDIF из таблицы + адрес буфера + формат. */
static void build_elcdif_cfg(const display_hw_cfg_t *p_cfg, uint32_t framebuffer_addr,
                             bsp_display_pixel_format_t format, elcdif_rgb_mode_config_t *p_out)
{
    p_out->panelWidth    = p_cfg->width;
    p_out->panelHeight   = p_cfg->height;
    p_out->hsw           = p_cfg->hsw;
    p_out->hfp           = p_cfg->hfp;
    p_out->hbp           = p_cfg->hbp;
    p_out->vsw           = p_cfg->vsw;
    p_out->vfp           = p_cfg->vfp;
    p_out->vbp           = p_cfg->vbp;
    p_out->polarityFlags = p_cfg->pol_flags;
    p_out->bufferAddr    = framebuffer_addr;
    p_out->pixelFormat   = map_pixel_format(format);
    p_out->dataBus       = kELCDIF_DataBus24Bit;
}

/* ── Реализация API ───────────────────────────────────────────────────── */

bsp_status_t bsp_display_init(bsp_display_type_t type, uint32_t framebuffer_addr,
                              bsp_display_frame_cb_t p_on_frame_done)
{
    /* Обёртка совместимости — формат по умолчанию XRGB8888 (старые потребители). */
    return bsp_display_init_ex(type, framebuffer_addr, p_on_frame_done,
                               BSP_DISPLAY_PIXEL_XRGB8888);
}

bsp_status_t bsp_display_init_ex(bsp_display_type_t type, uint32_t framebuffer_addr,
                                 bsp_display_frame_cb_t p_on_frame_done,
                                 bsp_display_pixel_format_t format)
{
    if ((uint32_t) type >= (uint32_t) BSP_DISPLAY_COUNT)
    {
        return BSP_ERR_PARAM;
    }

    if (g_s_display.initialised)
    {
        return BSP_OK; /* идемпотентен */
    }

    const display_hw_cfg_t *p_cfg = &K_HW_CFG[type];

    bsp_status_t status = init_pixelclock(p_cfg);
    if (status != BSP_OK)
    {
        return status;
    }

    CLOCK_EnableClock(kCLOCK_LcdPixel);
    init_backlight();

    if (p_cfg->has_orientation_pins)
    {
        init_orientation_pins();
        init_mode_dither_pins();
    }

    /* Сохранить callback ДО включения IRQ — исключить гонку */
    g_s_display.frame_cb = p_on_frame_done;

    elcdif_rgb_mode_config_t elcdif_cfg;
    build_elcdif_cfg(p_cfg, framebuffer_addr, format, &elcdif_cfg);
    ELCDIF_RgbModeInit(LCDIF, &elcdif_cfg);
    enable_lcd_interrupt();
    ELCDIF_RgbModeStart(LCDIF);

    g_s_display.type        = type;
    g_s_display.size.width  = p_cfg->width;
    g_s_display.size.height = p_cfg->height;
    g_s_display.initialised = true;

    return BSP_OK;
}

bsp_status_t bsp_display_deinit(void)
{
    if (g_s_display.initialised)
    {
        ELCDIF_RgbModeStop(LCDIF);
        DisableIRQ(LCDIF_IRQn);
        ELCDIF_Deinit(LCDIF);
        CLOCK_DisableClock(kCLOCK_LcdPixel);
        GPIO_PinWrite(DISPLAY_BACKLIGHT_GPIO, DISPLAY_BACKLIGHT_PIN, 0U);

        g_s_display.frame_cb    = NULL;
        g_s_display.type        = BSP_DISPLAY_COUNT;
        g_s_display.initialised = false;
    }
    return BSP_OK;
}

bsp_status_t bsp_display_set_rotation(bsp_display_rotation_t rotation)
{
    if (!g_s_display.initialised)
    {
        return BSP_ERR_INIT;
    }

    const display_hw_cfg_t *p_cfg = &K_HW_CFG[g_s_display.type];

    if (!p_cfg->has_orientation_pins)
    {
        return (rotation == BSP_DISPLAY_ROTATE_0) ? BSP_OK : BSP_ERR_NOT_SUPPORTED;
    }

    uint8_t lr_value;
    uint8_t ud_value;

    switch (rotation)
    {
    case BSP_DISPLAY_ROTATE_0:
        lr_value = 1U;
        ud_value = 0U;
        break;
    case BSP_DISPLAY_FLIP_VERTICAL:
        lr_value = 1U;
        ud_value = 1U;
        break;
    case BSP_DISPLAY_FLIP_BOTH:
        lr_value = 0U;
        ud_value = 1U;
        break;
    case BSP_DISPLAY_FLIP_HORIZONTAL:
        lr_value = 0U;
        ud_value = 0U;
        break;
    default:
        return BSP_ERR_PARAM;
    }

    GPIO_PinWrite(DISPLAY_lr_value_GPIO, DISPLAY_lr_value_PIN, lr_value);
    GPIO_PinWrite(DISPLAY_UD_GPIO, DISPLAY_UD_PIN, ud_value);
    return BSP_OK;
}

void bsp_display_set_next_buffer(uint32_t framebuffer_addr)
{
    ELCDIF_SetNextBufferAddr(LCDIF, framebuffer_addr);
}

const bsp_display_size_t *bsp_display_get_size(void)
{
    return g_s_display.initialised ? &g_s_display.size : NULL;
}

bsp_display_type_t bsp_display_get_type(void)
{
    return g_s_display.type;
}