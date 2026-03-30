/*
 * bsp_opto — реализация
 *
 * Изменения относительно предыдущей версии:
 *
 * 1. Полярность: оптопары неинвертирующие → ACTIVE = HIGH = 1.
 *
 * 2. MODE_LEVEL (IN1, IN2): ISR переключает направление прерывания
 *    после каждого фронта (RISING↔FALLING), поэтому ловятся оба края.
 *    bsp_opto_process() обрабатывает только каналы с pending == true.
 *
 * 3. MODE_PROTO (RS): коллбэк вызывается прямо из ISR без дебаунса.
 *    После срабатывания прерывание канала отключается.
 *    bsp_opto_proto_arm() взводит его снова когда декодер готов.
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

/* Active-HIGH: оптопара неинвертирующая → ACTIVE = пин HIGH = raw 1 */
#define OPTO_PIN_TO_STATE(raw) ((raw) == 1U ? BSP_OPTO_STATE_ACTIVE : BSP_OPTO_STATE_INACTIVE)

/* ── Состояние канала ────────────────────────────────────────────────────── */

typedef struct
{
    volatile uint32_t last_edge_ms; /* bsp_tick_get_ms() в момент фронта  */
    volatile bool pending;     /* фронт зафиксирован, ждём дебаунс   */
    volatile uint32_t raw_pin; /* raw состояние пина в момент фронта  */
    bsp_opto_state_t confirmed_state; /* последнее подтверждённое состояние  */
    uint8_t pin;                      /* номер пина в GPIO1                  */
    bsp_opto_ch_mode_t mode;   /* режим канала                        */
    bsp_opto_edge_t next_edge; /* следующий ожидаемый фронт (LEVEL)   */
    bool enabled;              /* канал активен                       */
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

static gpio_interrupt_mode_t edge_to_irq_mode(bsp_opto_edge_t edge)
{
    return (edge == BSP_OPTO_EDGE_RISING) ? kGPIO_IntRisingEdge : kGPIO_IntFallingEdge;
}

static bsp_opto_edge_t opposite_edge(bsp_opto_edge_t edge)
{
    return (edge == BSP_OPTO_EDGE_RISING) ? BSP_OPTO_EDGE_FALLING : BSP_OPTO_EDGE_RISING;
}

/*
 * Настроить и включить прерывание на заданный фронт.
 * Используется как при инициализации, так и при взводе proto_arm().
 */
static void set_irq_edge(uint8_t pin, bsp_opto_edge_t edge)
{
    GPIO_SetPinInterruptConfig(OPTO_GPIO_BASE, pin, edge_to_irq_mode(edge));
    GPIO_EnableInterrupts(OPTO_GPIO_BASE, 1UL << pin);
}

/* ── ISR ─────────────────────────────────────────────────────────────────── */

/*
 * Общий обработчик для GPIO1[16..31].
 * Все три канала (пины 21, 22, 23) попадают сюда.
 *
 * MODE_LEVEL:
 *   — Фиксируем raw + timestamp, взводим pending.
 *   — Переключаем направление прерывания на противоположный фронт,
 *     чтобы поймать возврат сигнала.
 *
 * MODE_PROTO:
 *   — Сразу вызываем коллбэк (ISR-контекст, коллбэк должен быть ISR-safe).
 *   — Отключаем прерывание канала — декодер взведёт его снова через
 *     bsp_opto_proto_arm() когда будет готов принять следующий пакет.
 */
void GPIO1_Combined_16_31_IRQHandler(void) // NOLINT(readability-identifier-naming)
{
    uint32_t flags = GPIO_GetPinsInterruptFlags(OPTO_GPIO_BASE);

    for (bsp_opto_ch_t ch = (bsp_opto_ch_t) 0U; ch < BSP_OPTO_CH_COUNT; ch++)
    {
        opto_ch_state_t *p_ch = &g_s_opto.channels[ch];

        if (!p_ch->enabled)
        {
            continue;
        }

        uint32_t mask = 1UL << p_ch->pin;
        if ((flags & mask) == 0U)
        {
            continue;
        }

        /* Сбрасываем флаг прерывания сразу */
        GPIO_ClearPinsInterruptFlags(OPTO_GPIO_BASE, mask);

        p_ch->raw_pin = read_pin(p_ch->pin);

        if (p_ch->mode == BSP_OPTO_MODE_PROTO)
        {
            /* Отключить прерывание — декодер взведёт его через proto_arm() */
            GPIO_DisableInterrupts(OPTO_GPIO_BASE, mask);

            /* Вызвать коллбэк прямо из ISR */
            if (g_s_opto.callbacks[ch] != NULL)
            {
                g_s_opto.callbacks[ch](ch, OPTO_PIN_TO_STATE(p_ch->raw_pin));
            }
        }
        else /* BSP_OPTO_MODE_LEVEL */
        {
            p_ch->last_edge_ms = bsp_tick_get_ms();
            p_ch->pending      = true;

            /* Переключить направление: следующий фронт — противоположный */
            p_ch->next_edge = opposite_edge(p_ch->next_edge);
            GPIO_SetPinInterruptConfig(OPTO_GPIO_BASE, p_ch->pin,
                                       edge_to_irq_mode(p_ch->next_edge));
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
    for (bsp_opto_ch_t ch = (bsp_opto_ch_t) 0U; ch < BSP_OPTO_CH_COUNT; ch++)
    {
        g_s_opto.callbacks[ch] = p_config->callbacks[ch];
    }
    g_s_opto.debounce_ms = p_config->debounce_ms;

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

    for (bsp_opto_ch_t ch = (bsp_opto_ch_t) 0U; ch < BSP_OPTO_CH_COUNT; ch++)
    {
        opto_ch_state_t *p_ch = &g_s_opto.channels[ch];

        if (ch == BSP_OPTO_CH_RS && !p_config->rs_as_gpio)
        {
            p_ch->enabled = false;
            continue;
        }

        p_ch->pin     = K_PINS[ch];
        p_ch->mode    = p_config->modes[ch];
        p_ch->enabled = true;
        p_ch->pending = false;

        if (ch == BSP_OPTO_CH_RS)
        {
            BOARD_InitRS_GPIO();
        }

        GPIO_PinInit(OPTO_GPIO_BASE, p_ch->pin, &gpio_in_cfg);

        if (p_ch->mode == BSP_OPTO_MODE_LEVEL)
        {
            /*
             * Для MODE_LEVEL начальный фронт выбираем по текущему состоянию пина:
             *   пин LOW  → ждём RISING  (сигнал ещё не пришёл)
             *   пин HIGH → ждём FALLING (сигнал уже активен)
             * Это гарантирует, что мы не пропустим первое изменение.
             */
            uint32_t current_raw  = read_pin(p_ch->pin);
            p_ch->confirmed_state = OPTO_PIN_TO_STATE(current_raw);
            p_ch->next_edge = (current_raw == 0U) ? BSP_OPTO_EDGE_RISING : BSP_OPTO_EDGE_FALLING;
            set_irq_edge(p_ch->pin, p_ch->next_edge);
        }
        else /* BSP_OPTO_MODE_PROTO */
        {
            /*
             * Для MODE_PROTO используем фронт из конфигурации —
             * обычно RISING (старт-бит протокола).
             * confirmed_state не используется для этого режима.
             */
            p_ch->confirmed_state = BSP_OPTO_STATE_INACTIVE;
            p_ch->next_edge       = p_config->edges[ch];
            set_irq_edge(p_ch->pin, p_ch->next_edge);
        }
    }

    EnableIRQ(GPIO1_Combined_16_31_IRQn);

    g_s_opto.initialised = true;
    return BSP_OK;
}

/* ── Чтение состояния ────────────────────────────────────────────────────── */

bsp_opto_state_t bsp_opto_read(bsp_opto_ch_t input_channel)
{
    if (input_channel >= BSP_OPTO_CH_COUNT || !g_s_opto.channels[input_channel].enabled ||
        g_s_opto.channels[input_channel].mode == BSP_OPTO_MODE_PROTO)
    {
        return BSP_OPTO_STATE_INACTIVE;
    }
    return g_s_opto.channels[input_channel].confirmed_state;
}

/* ── Взвод MODE_PROTO для следующего пакета ──────────────────────────────── */

void bsp_opto_proto_arm(bsp_opto_ch_t ch)
{
    if (ch >= BSP_OPTO_CH_COUNT)
    {
        return;
    }

    opto_ch_state_t *p_ch = &g_s_opto.channels[ch];

    if (!p_ch->enabled || p_ch->mode != BSP_OPTO_MODE_PROTO)
    {
        return;
    }

    /* Сбросить старый флаг прерывания и взвести снова */
    GPIO_ClearPinsInterruptFlags(OPTO_GPIO_BASE, 1UL << p_ch->pin);
    set_irq_edge(p_ch->pin, p_ch->next_edge);
}

/* ── Обработка дебаунса (только MODE_LEVEL) ──────────────────────────────── */

void bsp_opto_process(void)
{
    if (!g_s_opto.initialised)
    {
        return;
    }

    uint32_t now = bsp_tick_get_ms();

    for (bsp_opto_ch_t ch = (bsp_opto_ch_t) 0U; ch < BSP_OPTO_CH_COUNT; ch++)
    {
        opto_ch_state_t *p_ch = &g_s_opto.channels[ch];

        /* Пропускаем: выключен, нет события, или proto-канал (обрабатывается в ISR) */
        if (!p_ch->enabled || !p_ch->pending || p_ch->mode == BSP_OPTO_MODE_PROTO)
        {
            continue;
        }

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