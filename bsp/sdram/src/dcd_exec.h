/**
 * @file  dcd_exec.h
 * @brief Интерпретатор DCD (Device Configuration Data) — тот же формат, что
 *        исполняет BootROM до передачи управления образу.
 *
 * Зачем: один и тот же DCD-массив (источник истины — DCD Tool в Config Tools)
 * исполняется и BootROM-ом (HAB-образы firmware_test/tft_app), и в рантайме
 * (bootloader без DCD, HIL-стресс-тесты SDRAM). Ручной порт регистров в C
 * больше не нужен — расхождение DCD и C-кода невозможно по построению.
 *
 * Формат — i.MX RT1050 RM Rev.4, §9.7.2:
 *   Header      : tag 0xD2 | length (BE16, вкл. заголовок) | version 0x41
 *   Write data  : tag 0xCC | len | par, затем пары {address, value/mask} (BE32)
 *   Check data  : tag 0xCF | len | par, address, mask [, count]
 *   NOP         : tag 0xC0 | len = 4
 *   par         : bits[2:0] — ширина (1/2/4 байта), bit3 — Mask, bit4 — Set
 *
 * Семантика флагов — RM табл. 9-41 (write) и 9-45 (check).
 *
 * Отличия от BootROM (сознательные):
 *   - DCD целиком валидируется ДО исполнения первой команды: битый массив не
 *     исполняется частично.
 *   - Check без count у BootROM опрашивает бесконечно; здесь — до таймаута
 *     (dcd_exec_io_t.check_timeout_ms), чтобы не повесить прошивку.
 *   - Белый список адресов (RM табл. 9-42) не проверяется — это задача
 *     офлайн-конвертера DCD, а не рантайма.
 *
 * Платформо-независим: весь доступ к регистрам и времени — через dcd_exec_io_t,
 * поэтому модуль тестируется на хосте (tests/host/dcd_exec).
 */

#ifndef DCD_EXEC_H_
#define DCD_EXEC_H_

#include "bsp/status.h"

#include <stddef.h>
#include <stdint.h>

/** @brief Максимальный размер DCD, байт (RM §9.7.2). */
#define DCD_EXEC_MAX_SIZE 1768U

/**
 * @brief Доступ к регистрам и времени для интерпретатора.
 *
 * width — 1, 2 или 4 байта; адрес уже проверен на выравнивание.
 */
typedef struct dcd_exec_io_s
{
    uint32_t (*read)(uint32_t addr, uint8_t width);
    void (*write)(uint32_t addr, uint32_t value, uint8_t width);
    uint32_t (*now_ms)(void);
    uint32_t check_timeout_ms; /**< Предел опроса одной Check-команды, мс. */
    /** Необязательно (NULL): вызывается перед каждым элементом со смещением от начала DCD —
     *  для write это смещение пары address/value, для check/NOP — заголовка команды.
     *  Нужно, чтобы при зависании на обращении к регистру было видно, на каком. */
    void (*on_command)(size_t offset);
} dcd_exec_io_t;

/**
 * @brief Диагностика прогона — где остановились и сколько сделали.
 */
typedef struct dcd_exec_result_s
{
    size_t offset;       /**< Смещение команды, на которой остановились; при успехе — длина DCD. */
    uint32_t commands;   /**< Число исполненных (или, для validate, проверенных) команд. */
    uint32_t writes;     /**< Число выполненных записей (пар адрес/значение). */
} dcd_exec_result_t;

/**
 * @brief Проверить структуру DCD, ничего не исполняя.
 *
 * @param p_dcd     DCD-массив (заголовок + команды).
 * @param size      Размер буфера, байт (может быть больше длины из заголовка).
 * @param p_result  Диагностика; NULL — не нужна.
 *
 * @retval BSP_OK                DCD корректен.
 * @retval BSP_ERR_PARAM         p_dcd == NULL.
 * @retval BSP_ERR_INVALID       Битый заголовок/команда, невыровненный адрес,
 *                               значение шире ширины доступа.
 * @retval BSP_ERR_NOT_SUPPORTED Неизвестная команда (например, Unlock 0xB2).
 */
bsp_status_t dcd_exec_validate(const uint8_t *p_dcd, size_t size, dcd_exec_result_t *p_result);

/**
 * @brief Проверить и исполнить DCD.
 *
 * @param p_dcd     DCD-массив.
 * @param size      Размер буфера, байт.
 * @param p_io      Доступ к регистрам и времени (все поля не NULL).
 * @param p_result  Диагностика; NULL — не нужна.
 *
 * @retval BSP_OK           Все команды исполнены.
 * @retval BSP_ERR_TIMEOUT  Check-команда не дождалась условия (count или таймаут);
 *                          p_result->offset указывает на неё, дальнейшие команды
 *                          не исполнялись.
 * @retval прочие           Как у dcd_exec_validate(); ни одна команда не исполнена.
 */
bsp_status_t dcd_exec_run(const uint8_t *p_dcd, size_t size, const dcd_exec_io_t *p_io,
                          dcd_exec_result_t *p_result);

#endif /* DCD_EXEC_H_ */
