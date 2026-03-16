#pragma once

/**
 * Stub fsl_gpio.h для host-тестов.
 * Содержит только типы и сигнатуры используемые в bsp/led/src/led.c.
 * fff предоставляет реализации через FAKE_VOID_FUNC.
 */

#include <stdint.h>

/* GPIO peripheral base pointer type */
typedef struct
{
    uint32_t reserved[64];
} GPIO_Type;

/* Pin direction */
typedef enum
{
    kGPIO_DigitalInput  = 0U,
    kGPIO_DigitalOutput = 1U,
} gpio_pin_direction_t;

/* Interrupt mode — не используется в LED, но нужен для компиляции */
typedef enum
{
    kGPIO_NoIntmode = 0U,
} gpio_interrupt_mode_t;

/* Pin config struct */
typedef struct
{
    gpio_pin_direction_t direction;
    uint8_t outputLogic;
    gpio_interrupt_mode_t interruptMode;
} gpio_pin_config_t;

/* Functions — реализуются через fff в тест-файле */
void GPIO_PinInit(GPIO_Type *base, uint32_t pin, const gpio_pin_config_t *config);
void GPIO_PinWrite(GPIO_Type *base, uint32_t pin, uint8_t output);