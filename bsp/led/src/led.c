#include "bsp/led.h"

#include "fsl_gpio.h"
#include "pin_mux.h"

#include <stddef.h>

/**
 * Пины взяты из generated/pin_mux.h — имена макросов должны
 * совпадать с тем что сгенерировал MCUXpresso Config Tools.
 *
 * Пины из generated/pin_mux.h (MCUXpresso Config Tools):
 *
 *   UserLed1 → GPIO3 pin 3  (coord M4, GPIO_SD_B1_03) — LED_HEARTBEAT
 *   UserLed2 → GPIO3 pin 4  (coord P2, GPIO_SD_B1_04) — LED_APP
 *
 * INIT_GPIO_VALUE = 1U → active LOW подтверждён схемой.
 */

/* ── Дескриптор одного светодиода ───────────────────────────────────── */

typedef struct
{
    GPIO_Type *gpio;
    uint32_t pin;
    bool state; /**< true = горит (логическое состояние) */
} led_desc_t;

/* ── Таблица светодиодов ─────────────────────────────────────────────
 * Порядок должен совпадать с led_id_t.
 * ─────────────────────────────────────────────────────────────────── */
static led_desc_t s_leds[] = {
    [LED_HEARTBEAT] = {
        .gpio  = BOARD_INITPINS_UserLed1_GPIO,
        .pin   = BOARD_INITPINS_UserLed1_GPIO_PIN,
        .state = false,
    },
    [LED_APP] = {
        .gpio  = BOARD_INITPINS_UserLed2_GPIO,
        .pin   = BOARD_INITPINS_UserLed2_GPIO_PIN,
        .state = false,
    },
};

#define LED_COUNT (sizeof(s_leds) / sizeof(s_leds[0]))

/* ── Приватные хелперы ───────────────────────────────────────────────── */

/** Перевести логическое состояние в физический уровень GPIO (active LOW). */
static inline uint8_t led_to_gpio_level(bool is_enabled)
{
    return is_enabled ? 0U : 1U;
}

static inline void led_apply(led_desc_t *p_led)
{
    GPIO_PinWrite(p_led->gpio, p_led->pin, led_to_gpio_level(p_led->state));
}

/* ── Публичный API ───────────────────────────────────────────────────── */

void led_init(void)
{
    gpio_pin_config_t cfg = {
        .direction     = kGPIO_DigitalOutput,
        .outputLogic   = 1U, /* active LOW → начинаем с 1 = LED выключен */
        .interruptMode = kGPIO_NoIntmode,
    };

    for (size_t i = 0; i < LED_COUNT; i++)
    {
        GPIO_PinInit(s_leds[i].gpio, s_leds[i].pin, &cfg);
        s_leds[i].state = false;
    }
}

void led_on(led_id_t led_id)
{
    led_set(led_id, true);
}

void led_off(led_id_t led_id)
{
    led_set(led_id, false);
}

void led_toggle(led_id_t led_id)
{
    led_set(led_id, !s_leds[led_id].state);
}

void led_set(led_id_t led_id, bool is_enabled)
{
    s_leds[led_id].state = is_enabled;
    led_apply(&s_leds[led_id]);
}

bool led_get(led_id_t led_id)
{
    return s_leds[led_id].state;
}