/*
 * bsp_opto — реализация
 */

#include "bsp/opto.h"

#include "board.h"
#include "bsp/tick.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "pin_mux.h"

#include <stddef.h>
#include <stdint.h>

/* ── Константы пинов ─────────────────────────────────────────────────────── */

/* Все три пина — GPIO1, банк [16..31] → GPIO1_Combined_16_31_IRQn */

#define OPTO_GPIO_BASE GPIO1

#define OPTO_IN1_PIN 22U
#define OPTO_IN2_PIN 21U
#define OPTO_RS_PIN  23U

//FIXME: fixed (оптопара неинтвертирующая: когда на входе 1, на выходе тоже будет 1)
/* Active-HIGH: оптопара тянет пин к VCC → ACTIVE = HIGH = 1 */
#define OPTO_PIN_TO_STATE(raw) ((raw) == 1U ? BSP_OPTO_STATE_ACTIVE : BSP_OPTO_STATE_INACTIVE)

/* ── Состояние канала ────────────────────────────────────────────────────── */

typedef struct
{
    volatile uint32_t last_edge_ms; /* bsp_tick_get_ms() в момент фронта */
    volatile bool pending;     /* фронт зафиксирован, ждём дебаунс  */
    volatile uint32_t raw_pin; /* raw состояние пина в момент фронта */
    bsp_opto_state_t confirmed_state; /* последнее подтверждённое состояние */
    uint8_t pin;                      /* номер пина в GPIO1                 */
    bool enabled;                     /* канал активен                      */
} opto_ch_state_t;

/* ── Модульное состояние ─────────────────────────────────────────────────── */

typedef struct
{
    opto_ch_state_t channels[BSP_OPTO_CH_COUNT];
    bsp_opto_callback_t callbacks[BSP_OPTO_CH_COUNT];
    uint32_t debounce_ms;
    bool initialised;
} opto_module_t;

static opto_module_t g_s_opto;

/* ── Вспомогательные функции ─────────────────────────────────────────────── */

static uint32_t read_pin(uint8_t pin)
{
    return GPIO_PinRead(OPTO_GPIO_BASE, pin);
}

static void enable_irq(uint8_t pin, bsp_opto_edge_t edge)
{
    gpio_interrupt_mode_t mode =
        (edge == BSP_OPTO_EDGE_RISING) ? kGPIO_IntRisingEdge : kGPIO_IntFallingEdge;
    GPIO_SetPinInterruptConfig(OPTO_GPIO_BASE, pin, mode);
    GPIO_EnableInterrupts(OPTO_GPIO_BASE, 1UL << pin);
}

/* ── ISR ─────────────────────────────────────────────────────────────────── */

/*
 * Общий обработчик для GPIO1[16..31].
 * Все три канала (пины 21, 22, 23) попадают сюда.
 */

void GPIO1_Combined_16_31_IRQHandler(void) // NOLINT(readability-identifier-naming)
{
    uint32_t flags = GPIO_GetPinsInterruptFlags(OPTO_GPIO_BASE);

    for (bsp_opto_ch_t ch = 0U; ch < BSP_OPTO_CH_COUNT; ch++)
    {
        opto_ch_state_t *p_ch = &g_s_opto.channels[ch];

        if (!p_ch->enabled)
        {
            continue;
        }

        uint32_t mask = 1UL << p_ch->pin;
        if ((flags & mask) != 0U)
        {
            p_ch->raw_pin      = read_pin(p_ch->pin);
            p_ch->last_edge_ms = bsp_tick_get_ms();
            p_ch->pending      = true;

            GPIO_ClearPinsInterruptFlags(OPTO_GPIO_BASE, mask);
        }
    }

    SDK_ISR_EXIT_BARRIER;
}

/* ── Инициализация ───────────────────────────────────────────────────────── */

bsp_status_t bsp_opto_init(const bsp_opto_config_t *p_config)
{
    if (p_config == NULL)
    {
        return BSP_ERR_PARAM;
    }

    /* Сохранить конфигурацию */
    for (bsp_opto_ch_t ch = 0U; ch < BSP_OPTO_CH_COUNT; ch++)
    {
        g_s_opto.callbacks[ch] = p_config->callbacks[ch];
    }
    g_s_opto.debounce_ms = p_config->debounce_ms;

    /* Настройка каналов IN1, IN2 — всегда активны */
    static const uint8_t K_PINS[BSP_OPTO_CH_COUNT] = {
        [BSP_OPTO_CH_IN1] = OPTO_IN1_PIN,
        [BSP_OPTO_CH_IN2] = OPTO_IN2_PIN,
        [BSP_OPTO_CH_RS]  = OPTO_RS_PIN,
    };

    gpio_pin_config_t gpio_in_cfg = {
        .direction     = kGPIO_DigitalInput,
        .outputLogic   = 0U,
        .interruptMode = kGPIO_NoIntmode,
    };

    for (bsp_opto_ch_t ch = 0U; ch < BSP_OPTO_CH_COUNT; ch++)
    {
        opto_ch_state_t *p_ch = &g_s_opto.channels[ch];

        if (ch == BSP_OPTO_CH_RS && !p_config->rs_as_gpio)
        {
            p_ch->enabled = false;
            continue;
        }

        p_ch->pin     = K_PINS[ch];
        p_ch->enabled = true;
        p_ch->pending = false;

        /* Начальное состояние — читаем пин до включения прерываний */
        if (ch == BSP_OPTO_CH_RS)
        {
            BOARD_InitRS_GPIO();
        }

        GPIO_PinInit(OPTO_GPIO_BASE, p_ch->pin, &gpio_in_cfg);
        p_ch->confirmed_state = OPTO_PIN_TO_STATE(read_pin(p_ch->pin));

        enable_irq(p_ch->pin, p_config->edges[ch]);
    }

    /* Включить IRQ GPIO1_Combined_16_31 */
    EnableIRQ(GPIO1_Combined_16_31_IRQn);

    g_s_opto.initialised = true;
    return BSP_OK;
}

/* ── Чтение состояния ────────────────────────────────────────────────────── */

bsp_opto_state_t bsp_opto_read(bsp_opto_ch_t input_channel)
{
    if (input_channel >= BSP_OPTO_CH_COUNT || !g_s_opto.channels[input_channel].enabled)
    {
        return BSP_OPTO_STATE_INACTIVE;
    }
    return g_s_opto.channels[input_channel].confirmed_state;
}

/* ── Обработка дебаунса ──────────────────────────────────────────────────── */

void bsp_opto_process(void)
{
    if (!g_s_opto.initialised)
    {
        return;
    }

    uint32_t now = bsp_tick_get_ms();

    for (bsp_opto_ch_t ch = 0U; ch < BSP_OPTO_CH_COUNT; ch++)
    {
        opto_ch_state_t *p_ch = &g_s_opto.channels[ch];
        // TODO: разобраться как ловить ситуацию, когда сигнал отключается (Возможно нужно включить, как RISING, так и FALLING (кроме RS))
        // if (!p_ch->enabled || !p_ch->pending)
        // {
        //     continue;
        // }

        uint32_t elapsed = now - p_ch->last_edge_ms;
        if (elapsed < g_s_opto.debounce_ms)
        {
            continue;
        }

        /* Дебаунс истёк — перечитать пин и подтвердить состояние */
        bsp_opto_state_t new_state = OPTO_PIN_TO_STATE(read_pin(p_ch->pin));
        p_ch->pending              = false;

        if (new_state != p_ch->confirmed_state)
        {
            p_ch->confirmed_state = new_state;

            if (g_s_opto.callbacks[ch] != NULL)
            {
                g_s_opto.callbacks[ch](ch, new_state);
            }
        }
    }
}