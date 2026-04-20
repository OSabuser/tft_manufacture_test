/**
 * @file  sdram.c
 * @brief Верификация внешней SDRAM MT48LC16M16A2 (32 МБ, шина 16 бит).
 *
 * SEMC инициализируется DCD до main() — этот модуль только проверяет
 * доступность памяти. Регистры SEMC не модифицируются.
 *
 * Кэш: SDRAM настроена как Normal Write-Back cacheable (MPU Region 8).
 * Верификация требует явного cache maintenance перед readback — иначе
 * чтение попадает в кэш и физическая SDRAM не тестируется.
 */

#include "bsp/sdram.h"

#include "bsp/tick.h"
#include "fsl_semc.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Таймаут ожидания готовности SEMC контроллера, мс. */
#define SDRAM_SEMC_IDLE_TIMEOUT_MS 10U

/**
 * @brief Размер кэш-линии Cortex-M7, байт.
 *
 * SCB_CleanDCache_by_Addr / SCB_InvalidateDCache_by_Addr требуют
 * выравнивания адреса и размера по кэш-линии.
 * BSP_SDRAM_TEST_BASE_ADDR = 0x80200000 — кратен 32 байтам. ✓
 */
#define SDRAM_CACHE_LINE_BYTES 32U

/** @brief Эталонный паттерн верификации (чередующиеся биты). */
#define SDRAM_VERIFY_PATTERN_A 0xA5A5A5A5UL

/** @brief Инверсия эталонного паттерна. */
#define SDRAM_VERIFY_PATTERN_B 0x5A5A5A5AUL

/* ── Состояние модуля ──────────────────────────────────────────────────── */

static bool s_initialised = false;

/* ── Внутренние функции ────────────────────────────────────────────────── */

/**
 * @brief Дождаться перехода SEMC в состояние IDLE.
 *
 * @return BSP_OK при успехе, BSP_ERR_TIMEOUT если SEMC не ответил.
 */
static bsp_status_t wait_semc_idle(void)
{
    const uint32_t START_MS = bsp_tick_get_ms();

    while ((SEMC->STS0 & SEMC_STS0_IDLE_MASK) == 0U)
    {
        if ((bsp_tick_get_ms() - START_MS) >= SDRAM_SEMC_IDLE_TIMEOUT_MS)
        {
            return BSP_ERR_TIMEOUT;
        }
    }

    return BSP_OK;
}

/**
 * @brief Сбросить кэш-линию по адресу тестового региона.
 *
 * Clean (запись dirty линии в SDRAM) + Invalidate (следующее чтение
 * пойдёт в SDRAM, не в кэш) + DSB (барьер завершения операции).
 */
static void flush_cache_at_test_base(void)
{
    uint32_t *const p_addr = (uint32_t *) BSP_SDRAM_TEST_BASE_ADDR;

    SCB_CleanDCache_by_Addr(p_addr, (int32_t) SDRAM_CACHE_LINE_BYTES);
    SCB_InvalidateDCache_by_Addr(p_addr, (int32_t) SDRAM_CACHE_LINE_BYTES);
    __DSB();
}

/**
 * @brief Записать слово в тестовый адрес, сбросить кэш, прочитать обратно.
 *
 * @param[in]  pattern   Значение для записи и верификации.
 * @return BSP_OK если readback совпал, BSP_ERR_INIT при расхождении.
 */
static bsp_status_t verify_word(uint32_t pattern)
{
    volatile uint32_t *const p_test = (volatile uint32_t *) BSP_SDRAM_TEST_BASE_ADDR;

    *p_test = pattern;
    flush_cache_at_test_base();

    return (*p_test == pattern) ? BSP_OK : BSP_ERR_INIT;
}

/* ── Public API ────────────────────────────────────────────────────────── */

bsp_status_t bsp_sdram_init(void)
{
    bsp_status_t status = wait_semc_idle();
    if (status != BSP_OK)
    {
        return status;
    }

    status = verify_word(SDRAM_VERIFY_PATTERN_A);
    if (status != BSP_OK)
    {
        return BSP_ERR_INIT;
    }

    status = verify_word(SDRAM_VERIFY_PATTERN_B);
    if (status != BSP_OK)
    {
        return BSP_ERR_INIT;
    }

    s_initialised = true;
    return BSP_OK;
}
