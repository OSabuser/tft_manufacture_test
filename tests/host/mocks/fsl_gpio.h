#pragma once

/**
 * Stub fsl_gpio.h для host-тестов.
 * Содержит типы и сигнатуры используемые в:
 *   bsp/led/src/led.c
 *   bsp/opto/src/opto.c
 *
 * fff предоставляет реализации через FAKE_VOID_FUNC / FAKE_VALUE_FUNC.
 */

#include <stdint.h>

/* ── GPIO peripheral ─────────────────────────────────────────────────────── */

typedef struct
{
    uint32_t reserved[64];
} GPIO_Type;

/*
 * Stub-экземпляры и макросы GPIO1/2/3.
 * static — каждая translation unit получает свою копию (нормально для тестов).
 * #ifndef защита — на случай если pin_mux.h включён раньше.
 */
#ifndef GPIO1
static GPIO_Type stub_GPIO1;
static GPIO_Type stub_GPIO2;
static GPIO_Type stub_GPIO3;

#define GPIO1 (&stub_GPIO1)
#define GPIO2 (&stub_GPIO2)
#define GPIO3 (&stub_GPIO3)
#endif

/* ── Enums ───────────────────────────────────────────────────────────────── */

typedef enum
{
    kGPIO_DigitalInput  = 0U,
    kGPIO_DigitalOutput = 1U,
} gpio_pin_direction_t;

typedef enum
{
    kGPIO_NoIntmode              = 0U,
    kGPIO_IntLowLevel            = 1U,
    kGPIO_IntHighLevel           = 2U,
    kGPIO_IntRisingEdge          = 3U,
    kGPIO_IntFallingEdge         = 4U,
    kGPIO_IntRisingOrFallingEdge = 5U,
} gpio_interrupt_mode_t;

/* IRQ-номера — только те что нужны для opto */
typedef enum
{
    GPIO1_Combined_16_31_IRQn = 100,
} IRQn_Type;

/* ── Pin config ──────────────────────────────────────────────────────────── */

typedef struct
{
    gpio_pin_direction_t direction;
    uint8_t outputLogic;
    gpio_interrupt_mode_t interruptMode;
} gpio_pin_config_t;

/* ── CMSIS / SDK макросы ─────────────────────────────────────────────────── */

/* На хосте — no-op */
#define SDK_ISR_EXIT_BARRIER ((void) 0)

/* ── Function signatures — реализуются через fff ─────────────────────────── */

void GPIO_PinInit(GPIO_Type *base, uint32_t pin, const gpio_pin_config_t *config);
void GPIO_PinWrite(GPIO_Type *base, uint32_t pin, uint8_t output);
uint32_t GPIO_PinRead(GPIO_Type *base, uint32_t pin);

void GPIO_SetPinInterruptConfig(GPIO_Type *base, uint32_t pin,
                                gpio_interrupt_mode_t pinInterruptMode);
void GPIO_EnableInterrupts(GPIO_Type *base, uint32_t mask);
void GPIO_DisableInterrupts(GPIO_Type *base, uint32_t mask);
uint32_t GPIO_GetPinsInterruptFlags(GPIO_Type *base);
void GPIO_ClearPinsInterruptFlags(GPIO_Type *base, uint32_t mask);

void EnableIRQ(IRQn_Type irq);