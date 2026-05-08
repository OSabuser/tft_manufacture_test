/*
 * bsp_button — тактовые кнопки TactBut1 / TactBut2
 *
 * Аппаратура:
 *   TactBut1 — GPIO2 pin 30 (GPIO_B1_14), подтяжка к 3V3 внешняя, нажатие = LOW
 *   TactBut2 — GPIO2 pin 31 (GPIO_B1_15), подтяжка к 3V3 внешняя, нажатие = LOW
 *
 * Пины инициализированы в BOARD_InitPins() (generated/pin_mux.c).
 * bsp_button_init() не трогает GPIO — только сбрасывает внутреннее состояние.
 *
 * Использование (bare-metal):
 *   bsp_button_init();
 *   // в tick-коллбэке каждые 5 мс:
 *   bsp_button_poll();
 *   // в основном цикле:
 *   if (bsp_button_get_event_pressed(BSP_BUTTON_1)) { ... }
 *
 * Использование (FreeRTOS):
 *   // в таске с vTaskDelay(5):
 *   bsp_button_poll();
 *   if (bsp_button_get_event_pressed(BSP_BUTTON_1)) { xQueueSend(...); }
 */

#ifndef BSP_BUTTON_H
#define BSP_BUTTON_H

#include "bsp/status.h"

#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Типы
 * ---------------------------------------------------------------------- */

typedef enum
{
    BSP_BUTTON_1 = 0, /* TactBut1 — GPIO2/30 */
    BSP_BUTTON_2 = 1, /* TactBut2 — GPIO2/31 */
    BSP_BUTTON_COUNT,
} bsp_button_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/**
 * Инициализация модуля.
 * Сбрасывает внутреннее состояние debounce.
 * GPIO уже настроен в BOARD_InitPins() — вызывать после board_hw_init().
 */
bsp_status_t bsp_button_init(void);

/**
 * Мгновенное сырое чтение пина без debounce.
 * Предназначено для проверки при старте (например, bootloader hold-check).
 * Возвращает true если кнопка нажата прямо сейчас.
 */
bool bsp_button_read(bsp_button_t btn);

/**
 * Шаг debounce — вызывать строго каждые 5 мс.
 * Обновляет стабильное состояние и выставляет одноразовые события.
 * Безопасно вызывать из ISR (tick) или из таска FreeRTOS.
 */
void bsp_button_poll(void);

/**
 * Возвращает стабильное состояние после debounce.
 * true = кнопка удерживается нажатой.
 */
bool bsp_button_is_pressed(bsp_button_t btn);

/**
 * Одноразовое событие нажатия.
 * Возвращает true один раз после того как debounce зафиксировал переход
 * в нажатое состояние. Флаг сбрасывается при вызове.
 */
bool bsp_button_get_event_pressed(bsp_button_t btn);

/**
 * Одноразовое событие отпускания.
 * Возвращает true один раз после того как debounce зафиксировал переход
 * в отпущенное состояние. Флаг сбрасывается при вызове.
 */
bool bsp_button_get_event_released(bsp_button_t btn);

#endif /* BSP_BUTTON_H */