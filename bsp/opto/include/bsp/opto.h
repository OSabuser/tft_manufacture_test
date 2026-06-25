/*
 * bsp_opto — оптоизолированные входы
 *
 * Аппаратура:
 *   EXT_IN1  GPIO1[22]  GPIO_AD_B1_06  active-high (PS2801-4, неинвертирующая)
 *   EXT_IN2  GPIO1[21]  GPIO_AD_B1_05  active-high (PS2801-4, неинвертирующая)
 *   RS_RX    GPIO1[23]  GPIO_AD_B1_07  active-high (PS2801-4, неинвертирующая)
 *                       опциональный канал — только при rs_as_gpio == true
 *
 * Логика: active-HIGH. Оптопара неинвертирующая:
 *   Пин HIGH (ток есть)  → BSP_OPTO_STATE_ACTIVE
 *   Пин LOW  (тока нет)  → BSP_OPTO_STATE_INACTIVE
 *
 * Режимы каналов (bsp_opto_ch_mode_t):
 * ы
 *   BSP_OPTO_MODE_LEVEL  — IN1, IN2
 *     Детектирование уровня с программным дебаунсом.
 *     ISR переключает направление прерывания (RISING↔FALLING) после каждого фронта,
 *     чтобы ловить оба края. bsp_opto_process() вызывается из main loop и подтверждает
 *     стабильное состояние после истечения debounce_ms.
 *     Коллбэк вызывается из контекста main loop (НЕ из ISR).
 *
 *   BSP_OPTO_MODE_PROTO  — RS
 *     Детектирование одиночного фронта без дебаунса для приёма бинарного протокола.
 *     Коллбэк вызывается ПРЯМО ИЗ ISR — минимальная задержка.
 *     Коллбэк должен быть ISR-safe: только взводить флаг / писать в volatile-переменную.
 *     После каждого вызова прерывание отключается — принимающий модуль
 *     должен вызвать bsp_opto_proto_arm() для подготовки к следующему старт-биту.
 *
 * Использование (MODE_LEVEL):
 *   static void on_level_change(bsp_opto_ch_t ch, bsp_opto_state_t state) { ... }
 *
 *   bsp_opto_config_t cfg = {
 *       .callbacks   = { on_level_change, on_level_change, NULL },
 *       .modes       = { BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_PROTO },
 *       .rs_as_gpio  = true,
 *       .debounce_ms = 10U,
 *   };
 *   bsp_opto_init(&cfg);
 *
 *   // в main loop:
 *   bsp_opto_process();
 *
 * Использование (MODE_PROTO для RS):
 *   static volatile bool s_start_bit = false;
 *
 *   static void on_rs_start(bsp_opto_ch_t ch, bsp_opto_state_t state)
 *   {
 *       s_start_bit = true;   // только это — вызывается из ISR
 *   }
 *
 *   // После обработки старт-бита — взвести снова:
 *   bsp_opto_proto_arm(BSP_OPTO_CH_RS);
 */

#pragma once

#include "bsp/status.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Каналы ──────────────────────────────────────────────────────────────── */

typedef enum
{
    BSP_OPTO_CH_IN1 = 0U, /* EXT_IN1 — всегда доступен                  */
    BSP_OPTO_CH_IN2,      /* EXT_IN2 — всегда доступен                  */
    BSP_OPTO_CH_RS,       /* RS_RX   — только при rs_as_gpio == true     */
    BSP_OPTO_CH_COUNT,
} bsp_opto_ch_t;

/* ── Состояние канала ────────────────────────────────────────────────────── */

typedef enum
{
    BSP_OPTO_STATE_INACTIVE = 0U, /* тока нет, пин LOW  */
    BSP_OPTO_STATE_ACTIVE,        /* ток есть, пин HIGH */
} bsp_opto_state_t;

/* ── Режим работы канала ─────────────────────────────────────────────────── */

typedef enum
{
    /**
     * Детектирование уровня с дебаунсом.
     * Подходит для IN1, IN2.
     * ISR автоматически переключает направление (RISING↔FALLING).
     * Коллбэк вызывается из bsp_opto_process() — контекст main loop.
     */
    BSP_OPTO_MODE_LEVEL = 0U,

    /**
     * Детектирование одиночного фронта без дебаунса.
     * Подходит для RS при приёме бинарного протокола.
     * Коллбэк вызывается ПРЯМО ИЗ ISR.
     * После срабатывания прерывание отключается до вызова bsp_opto_proto_arm().
     * edges[ch] задаёт фронт начального старт-бита (обычно RISING).
     */
    BSP_OPTO_MODE_PROTO,
} bsp_opto_ch_mode_t;

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
 * Для MODE_LEVEL: вызывается из bsp_opto_process() — контекст main loop.
 * Для MODE_PROTO: вызывается из ISR — только атомарные операции!
 *
 * @param ch    канал, изменивший состояние
 * @param state новое состояние пина в момент фронта
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
     * Режим работы каждого канала.
     * MODE_LEVEL — дебаунс + коллбэк из main loop.
     * MODE_PROTO — без дебаунса, коллбэк из ISR.
     */
    bsp_opto_ch_mode_t modes[BSP_OPTO_CH_COUNT];

    /**
     * Начальный фронт прерывания.
     * MODE_LEVEL: игнорируется — ISR сам переключает направление.
     *             Начальный фронт выбирается автоматически по текущему
     *             состоянию пина при инициализации.
     * MODE_PROTO: фронт старт-бита (обычно BSP_OPTO_EDGE_RISING).
     */
    bsp_opto_edge_t edges[BSP_OPTO_CH_COUNT];

    /**
     * true  — RS_RX сконфигурировать как GPIO (BSP_OPTO_CH_RS активен).
     * false — RS_RX остаётся под управлением LPUART3.
     *         BSP_OPTO_CH_RS недоступен, его коллбэк игнорируется.
     */
    bool rs_as_gpio;

    /**
     * Период дебаунса в миллисекундах для MODE_LEVEL.
     * Рекомендуется 5–10 мс. 0 — дебаунс отключён (для тестирования).
     * Для MODE_PROTO не используется.
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
     * Конфигурирует пины, настраивает GPIO на вход, включает прерывания.
     * Для RS_RX вызывает BOARD_InitRS_GPIO() если rs_as_gpio == true.
     * Начальное состояние каналов MODE_LEVEL читается с пинов при инициализации.
     *
     * @param p_config  указатель на конфигурацию (не NULL)
     * @return BSP_OK или BSP_ERR_PARAM
     */
    bsp_status_t bsp_opto_init(const bsp_opto_config_t *p_config);

    /**
     * @brief Прочитать текущее подтверждённое состояние канала (только MODE_LEVEL).
     *
     * Для MODE_PROTO всегда возвращает BSP_OPTO_STATE_INACTIVE —
     * состояние RS отслеживается коллбэком из ISR.
     *
     * @param ch  номер канала
     * @return    BSP_OPTO_STATE_INACTIVE / BSP_OPTO_STATE_ACTIVE
     */
    bsp_opto_state_t bsp_opto_read(bsp_opto_ch_t input_channel);

    /**
     * @brief Обработать отложенные события дебаунса (только MODE_LEVEL).
     *
     * Должна вызываться из main loop на каждой итерации. НЕ вызывать из ISR.
     * Каналы MODE_PROTO пропускаются — для них используется коллбэк из ISR.
     */
    //FIXME: poll
    void bsp_opto_process(void);

    /**
     * @brief Взвести прерывание канала MODE_PROTO для приёма следующего старт-бита.
     *
     * После того как ISR сработал и вызвал коллбэк, прерывание канала
     * отключается. Принимающий модуль должен вызвать эту функцию когда
     * готов принять следующий пакет.
     *
     * Безопасно вызывать из main loop. Для каналов MODE_LEVEL — no-op.
     *
     * @param ch  канал (обычно BSP_OPTO_CH_RS)
     */
    void bsp_opto_proto_arm(bsp_opto_ch_t ch);

    /**
    * @brief Прочитать мгновенное состояние пина канала напрямую (без дебаунса).
    *
    * Используется после гарантированной стабилизации сигнала для синхронного
    * чтения в тестах. Обновляет confirmed_state.
    *
    * @param ch  канал (MODE_LEVEL)
    * @return    текущее состояние пина
    */
    bsp_opto_state_t bsp_opto_force_read(bsp_opto_ch_t ch);
#ifdef __cplusplus
}
#endif