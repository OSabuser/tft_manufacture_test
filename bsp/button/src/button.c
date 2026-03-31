#include "bsp/button.h"

#include "bsp/status.h"
#include "fsl_gpio.h"
#include "pin_mux.h"

#include <stddef.h>
#include <string.h>
/* -------------------------------------------------------------------------
 * Конфигурация debounce
 * ---------------------------------------------------------------------- */

/** Количество последовательных одинаковых сэмплов для фиксации состояния.
 *  При poll-периоде 5 мс: 4 × 5 мс = 20 мс. */
#define BUTTON_DEBOUNCE_SAMPLES 4U

/* -------------------------------------------------------------------------
 * Внутренние типы
 * ---------------------------------------------------------------------- */

typedef struct
{
    bool stable; /* текущее стабильное (после debounce) состояние */
    bool last_raw; /* последнее сырое значение для детектирования смены */
    uint8_t count; /* счётчик подтверждений текущего raw-значения */
    bool evt_pressed; /* одноразовый флаг: зафиксировано нажатие */
    bool evt_released; /* одноразовый флаг: зафиксировано отпускание */
} button_state_t;

/* -------------------------------------------------------------------------
 * Таблица пинов (индексируется по bsp_button_t)
 * ---------------------------------------------------------------------- */

typedef struct
{
    GPIO_Type *gpio;
    uint32_t pin;
} button_pin_t;

static const button_pin_t K_PINS[BSP_BUTTON_COUNT] = {
    [BSP_BUTTON_1] = { BOARD_INITPINS_TactBut1_GPIO, BOARD_INITPINS_TactBut1_GPIO_PIN },
    [BSP_BUTTON_2] = { BOARD_INITPINS_TactBut2_GPIO, BOARD_INITPINS_TactBut2_GPIO_PIN },
};

/* -------------------------------------------------------------------------
 * Состояние модуля
 * ---------------------------------------------------------------------- */

static button_state_t g_s_state[BSP_BUTTON_COUNT];
static bool g_s_initialized = false;

/* -------------------------------------------------------------------------
 * Реализация
 * ---------------------------------------------------------------------- */

bsp_status_t bsp_button_init(void)
{
    memset(g_s_state, 0, sizeof(g_s_state));
    g_s_initialized = true;
    return BSP_OK;
}

bool bsp_button_read(bsp_button_t btn)
{
    if ((uint32_t) btn >= (uint32_t) BSP_BUTTON_COUNT)
    {
        return false;
    }
    /* LOW = нажата (кнопка замыкает на GND, pull к 3V3 внешний) */
    return (GPIO_ReadPinInput(K_PINS[btn].gpio, K_PINS[btn].pin) == 0U);
}

void bsp_button_poll(void)
{
    if (!g_s_initialized)
    {
        return;
    }

    for (uint32_t i = 0U; i < (uint32_t) BSP_BUTTON_COUNT; i++)
    {
        button_state_t *button_state = &g_s_state[i];

        bool raw = (GPIO_ReadPinInput(K_PINS[i].gpio, K_PINS[i].pin) == 0U);

        if (raw == button_state->last_raw)
        {
            /* Сигнал стабилен — накапливаем подтверждения */
            if (button_state->count < BUTTON_DEBOUNCE_SAMPLES)
            {
                button_state->count++;
            }

            if (button_state->count == BUTTON_DEBOUNCE_SAMPLES && raw != button_state->stable)
            {
                /* Порог достигнут, состояние изменилось */
                button_state->stable = raw;
                if (raw)
                {
                    button_state->evt_pressed = true;
                }
                else
                {
                    button_state->evt_released = true;
                }
            }
        }
        else
        {
            /* Смена уровня — сбрасываем счётчик */
            button_state->count    = 1U;
            button_state->last_raw = raw;
        }
    }
}

bool bsp_button_is_pressed(bsp_button_t btn)
{
    if ((uint32_t) btn >= (uint32_t) BSP_BUTTON_COUNT)
    {
        return false;
    }
    return g_s_state[btn].stable;
}

bool bsp_button_get_event_pressed(bsp_button_t btn)
{
    if ((uint32_t) btn >= (uint32_t) BSP_BUTTON_COUNT)
    {
        return false;
    }
    bool evt                   = g_s_state[btn].evt_pressed;
    g_s_state[btn].evt_pressed = false;
    return evt;
}

bool bsp_button_get_event_released(bsp_button_t btn)
{
    if ((uint32_t) btn >= (uint32_t) BSP_BUTTON_COUNT)
    {
        return false;
    }
    bool evt                    = g_s_state[btn].evt_released;
    g_s_state[btn].evt_released = false;
    return evt;
}