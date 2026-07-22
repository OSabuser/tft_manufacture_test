/**
 * @file  display.h
 * @brief BSP: ELCDIF display driver — TFT4 / TFT7 / TFT8 / TFT10.
 *
 */

#ifndef BSP_DISPLAY_DISPLAY_H_
#define BSP_DISPLAY_DISPLAY_H_

#include "bsp/status.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Максимальные размеры (для статического выделения буферов) ───────── */

/** @brief Максимальная ширина среди поддерживаемых дисплеев (TFT7: 1024). */
#define BSP_DISPLAY_MAX_WIDTH 1024U

/** @brief Максимальная высота среди поддерживаемых дисплеев (TFT7/TFT8: 600). */
#define BSP_DISPLAY_MAX_HEIGHT 600U

/* ── Типы дисплеев ───────────────────────────────────────────────────── */

typedef enum bsp_display_type_e
{
    BSP_DISPLAY_TFT4 = 0U, /**< 480 × 272. Нет ножек ориентации/MODE/DITHB. */
    BSP_DISPLAY_TFT7,      /**< 1024 × 600. LR + UD + MODE + DITHB. */
    BSP_DISPLAY_TFT8,      /**< 800 × 600. LR + UD + MODE + DITHB. */
    BSP_DISPLAY_TFT10, /**< Зарезервировано — спецификации уточняются. */
    BSP_DISPLAY_COUNT,
} bsp_display_type_t;

/* ── Ориентация ──────────────────────────────────────────────────────── */

typedef enum bsp_display_rotation_e
{
    BSP_DISPLAY_ROTATE_0 = 0U, /**< LR=1 UD=0. Нормальная ориентация. */
    BSP_DISPLAY_FLIP_VERTICAL, /**< LR=1 UD=1. Вертикальный флип (UPDN). */
    BSP_DISPLAY_FLIP_BOTH, /**< LR=0 UD=1. Горизонтальный + вертикальный. */
    BSP_DISPLAY_FLIP_HORIZONTAL, /**< LR=0 UD=0. Горизонтальный флип (SHLR). */
} bsp_display_rotation_t;

/* ── Формат пикселя framebuffer'а (что ELCDIF читает из памяти) ───────── */

/**
 * @brief Формат пикселя в памяти framebuffer'а.
 *
 * Определяет ширину слова, которое ELCDIF читает по DMA из SDRAM (=нагрузку
 * на полосу). Ширина шины пинов панели (24-бит) — отдельный параметр драйвера,
 * от этого НЕ зависит: RGB565 (16 бит/пиксель в памяти) корректно выводится и
 * на 24-битную панель (ELCDIF расширяет 565→24 на пинах).
 */
typedef enum bsp_display_pixel_format_e
{
    BSP_DISPLAY_PIXEL_XRGB8888 = 0U, /**< 32 бита/пиксель (X игнорируется). По умолчанию. */
    BSP_DISPLAY_PIXEL_RGB565,        /**< 16 бит/пиксель — вдвое меньше полосы сканаута. */
} bsp_display_pixel_format_t;

/* ── Размер дисплея ──────────────────────────────────────────────────── */

typedef struct bsp_display_size_s
{
    uint16_t width;
    uint16_t height;
} bsp_display_size_t;

/* ── Callback FRAME_DONE ─────────────────────────────────────────────── */

/**
 * Вызывается из LCDIF ISR при завершении передачи кадра.
 *
 * Реализация ДОЛЖНА быть ISR-safe:
 *   — допустимо: запись в volatile bool, xSemaphoreGiveFromISR() и т.п.
 *   — запрещено: любые блокирующие вызовы.
 */
typedef void (*bsp_display_frame_cb_t)(void);

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Инициализировать ELCDIF для выбранного типа дисплея.
 *
 * Настраивает пиксельный клок, GPIO подсветки и ножки ориентации
 * (TFT7/TFT8/TFT10), запускает ELCDIF в RGB-режиме, включает IRQ FRAME_DONE.
 *
 * @note TFT4 требует инициализации Video PLL — возвращает BSP_ERR_NOT_SUPPORTED
 *       до реализации (TODO).
 * @note Вызывать однократно. Повторный вызов без deinit — no-op, не ошибка.
 *
 * @param type              Тип дисплея.
 * @param framebuffer_addr  Физический адрес первого фреймбуфера.
 *                          Должен быть выровнен по 64 байт, в NonCacheable SDRAM.
 * @param on_frame_done     ISR-safe callback по завершении кадра; NULL — без callback.
 * @return BSP_OK | BSP_ERR_PARAM | BSP_ERR_NOT_SUPPORTED
 *
 * @note Формат пикселя — XRGB8888 (обёртка над bsp_display_init_ex()).
 */
bsp_status_t bsp_display_init(bsp_display_type_t type, uint32_t framebuffer_addr,
                              bsp_display_frame_cb_t p_on_frame_done);

/**
 * @brief То же, что bsp_display_init(), но с явным форматом пикселя framebuffer'а.
 *
 * Позволяет потребителю выбрать RGB565 (вдвое меньше полосы сканаута) вместо
 * XRGB8888, не меняя остальную настройку. bsp_display_init() — обёртка с
 * форматом BSP_DISPLAY_PIXEL_XRGB8888 (совместимость со старыми потребителями).
 *
 * @param format  Формат пикселя в памяти (см. bsp_display_pixel_format_t).
 * @return BSP_OK | BSP_ERR_PARAM | BSP_ERR_NOT_SUPPORTED
 */
bsp_status_t bsp_display_init_ex(bsp_display_type_t type, uint32_t framebuffer_addr,
                                 bsp_display_frame_cb_t p_on_frame_done,
                                 bsp_display_pixel_format_t format);

/**
 * @brief Остановить ELCDIF, выключить подсветку, деинициализировать.
 *
 * Безопасен при вызове до bsp_display_init() или повторно.
 * Всегда возвращает BSP_OK.
 */
bsp_status_t bsp_display_deinit(void);

/**
 * @brief Переключить ориентацию через ножки LR/UD (TFT7/TFT8/TFT10).
 *
 * TFT4 поддерживает только ROTATE_0; остальные варианты → BSP_ERR_NOT_SUPPORTED.
 *
 * @return BSP_OK | BSP_ERR_NOT_SUPPORTED | BSP_ERR_INIT
 */
bsp_status_t bsp_display_set_rotation(bsp_display_rotation_t rotation);

/**
 * @brief Указать ELCDIF следующий фреймбуфер.
 *
 * Безопасен из ISR и из задачи. Переключение произойдёт аппаратно
 * по окончании текущего кадра.
 *
 * @param framebuffer_addr Физический адрес следующего буфера.
 */
void bsp_display_set_next_buffer(uint32_t framebuffer_addr);

/**
 * @brief Вернуть реальные размеры активного дисплея.
 *
 * @return Указатель на bsp_display_size_t; NULL до вызова bsp_display_init().
 */
const bsp_display_size_t *bsp_display_get_size(void);

/**
 * @brief Вернуть тип активного дисплея.
 *
 * @return BSP_DISPLAY_COUNT если не инициализирован.
 */
bsp_display_type_t bsp_display_get_type(void);

#endif /* BSP_DISPLAY_DISPLAY_H_ */