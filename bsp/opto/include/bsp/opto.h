/*
 * bsp_opto — оптоизолированные входы
 *
 * Аппаратура:
 *   EXT_IN1  GPIO1[22]  GPIO_AD_B1_06  active-low (PS2801-4, pull-up 1K к 3V3)
 *   EXT_IN2  GPIO1[21]  GPIO_AD_B1_05  active-low (PS2801-4, pull-up 1K к 3V3)
 *   RS_RX    GPIO1[23]  GPIO_AD_B1_07  active-low (PS2801-4, pull-up 1K к 3V3)
 *                       опциональный канал — только при rs_as_gpio == true
 *
 * Дебаунс: программный, на базе bsp_tick.
 *   ISR фиксирует timestamp + raw state → bsp_opto_process() из main loop
 *   подтверждает стабильное состояние и стреляет коллбэком.
 *
 * Использование:
 *   bsp_opto_config_t cfg = {
 *       .callbacks    = { my_cb, my_cb, NULL },
 *       .rs_as_gpio   = false,
 *       .debounce_ms  = 10U,
 *   };
 *   bsp_opto_init(&cfg);
 *
 *   // в main loop:
 *   bsp_opto_process();
 */

#pragma once

#include "bsp/status.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Каналы ──────────────────────────────────────────────────────────────── */

typedef enum
{
    BSP_OPTO_CH_IN1 = 0U, /* EXT_IN1 — всегда доступен */
    BSP_OPTO_CH_IN2,      /* EXT_IN2 — всегда доступен */
    BSP_OPTO_CH_RS,       /* RS_RX   — только при rs_as_gpio == true */
    BSP_OPTO_CH_COUNT,
} bsp_opto_ch_t;

/* ── Состояние канала ────────────────────────────────────────────────────── */

typedef enum
{
    BSP_OPTO_STATE_INACTIVE = 0U, /* цепь разомкнута, тока нет */
    BSP_OPTO_STATE_ACTIVE,        /* цепь замкнута, ток течёт */
} bsp_opto_state_t;

/* ── Фронт прерывания ────────────────────────────────────────────────────── */

typedef enum
{
    BSP_OPTO_EDGE_RISING = 0U,
    BSP_OPTO_EDGE_FALLING,
} bsp_opto_edge_t;

/* ── Коллбэк ─────────────────────────────────────────────────────────────── */

/**
 * @brief Коллбэк изменения состояния канала.
 *
 * Вызывается из bsp_opto_process() (контекст main loop, не ISR).
 * @param ch    канал, изменивший состояние
 * @param state новое подтверждённое (дебаунсированное) состояние
 */
typedef void (*bsp_opto_callback_t)(bsp_opto_ch_t input_channel, bsp_opto_state_t state);

/* ── Конфигурация ────────────────────────────────────────────────────────── */

typedef struct
{
    /**
     * Массив коллбэков, индекс = bsp_opto_ch_t.
     * NULL — коллбэк для данного канала не используется.
     */
    bsp_opto_callback_t callbacks[BSP_OPTO_CH_COUNT];

    /**
     * Фронт срабатывания прерывания
     * 
     */
    bsp_opto_edge_t edges[BSP_OPTO_CH_COUNT];

    /**
     * true  — RS_RX сконфигурировать как GPIO (BSP_OPTO_CH_RS активен).
     * false — RS_RX остаётся в состоянии по умолчанию (LPUART3 / AIN).
     *         BSP_OPTO_CH_RS недоступен, его коллбэк игнорируется.
     */
    bool rs_as_gpio;

    /**
     * Период дебаунса в миллисекундах. Рекомендуется 5–10 мс.
     * 0 — дебаунс отключён (для тестирования).
     */
    uint32_t debounce_ms;
} bsp_opto_config_t;

/* ── API ─────────────────────────────────────────────────────────────────── */

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * @brief Инициализировать модуль.
 *
 * Конфигурирует пины (pin mux), настраивает GPIO на вход,
 * включает прерывания по обоим фронтам. Для RS_RX вызывает
 * BOARD_InitRS_GPIO() если rs_as_gpio == true.
 *
 * @param p_config  указатель на конфигурацию (не NULL)
 * @return BSP_OK или BSP_ERR_INVALID_ARG
 */
    bsp_status_t bsp_opto_init(const bsp_opto_config_t *p_config);

    /**
 * @brief Прочитать текущее подтверждённое состояние канала.
 *
 * Возвращает последнее состояние, зафиксированное после дебаунса.
 * Может вызываться из любого контекста.
 *
 * @param ch  номер канала
 * @return    BSP_OPTO_STATE_INACTIVE / BSP_OPTO_STATE_ACTIVE
 */
    bsp_opto_state_t bsp_opto_read(bsp_opto_ch_t input_channel);

    /**
 * @brief Обработать отложенные события дебаунса.
 *
 * Должна вызываться из main loop на каждой итерации.
 * НЕ вызывать из ISR.
 *
 * Для каждого канала с pending-флагом:
 *   — если прошло >= debounce_ms с последнего фронта,
 *     перечитывает пин, сравнивает с confirmed_state,
 *     при изменении вызывает коллбэк и обновляет confirmed_state.
 */
    void bsp_opto_process(void);

#ifdef __cplusplus
}
#endif