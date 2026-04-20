/**
 * @file  test_sdram.c
 * @brief Тест-модуль firmware_test: SDRAM MT48LC16M16A2 (32 МБ, 16-bit bus).
 *
 * Четыре фазы (~30 с):
 *
 *   1. Address bus — 24 степени двойки (2^0..2^23 от TEST_BASE),
 *                    каждая с individual cache line flush.
 *                    Покрывает все 24 адресных бита MT48LC16M16A2
 *                    (13 row + 9 col + 2 bank). Время: < 1 мс.
 *
 *   2. Data bus   — walking ones + инверсия, 64 KB.
 *                   Ловит stuck-at faults на всех 8 битах шины данных.
 *                   Время: ~0.8 с.
 *
 *   3. Sequential — address pattern + инверсия, 2 MB.
 *                   Ловит coupling faults между соседними ячейками.
 *                   Время: ~24 с.
 *
 *   4. Retention  — address pattern, 256 KB: запись → flush → 200 мс → verify.
 *                   Ловит refresh timing failures.
 *                   Время: ~2 с.
 *
 * Кэш: тот же подход что в рабочей версии — SCB_CleanDCache_by_Addr + DSB.
 * USB: bsp_usb_cdc_poll() каждые SDRAM_USB_POLL_INTERVAL_BYTES.
 */

#include "bsp/sdram.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "fsl_common.h"
#include "test_module.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Интервал вызова bsp_usb_cdc_poll() при записи/чтении (4 KB). */
#define SDRAM_USB_POLL_INTERVAL_BYTES 0x1000U

/** @brief Ширина walking ones паттерна, бит. */
#define SDRAM_WALKING_ONES_BITS 8U

/**
 * @brief Количество адресных бит для address bus теста.
 *
 * MT48LC16M16A2: 13 row + 9 col + 2 bank = 24 бита.
 * Тестируем 2^0..2^23 байтовых смещений от TEST_BASE.
 * Максимальное смещение: 2^23 = 8 MB → 0x80200000 + 0x800000 = 0x80A00000
 * Граница SDRAM: 0x82000000. ✓
 */
#define SDRAM_ADDR_BUS_BITS 24U

/** @brief Размер кэш-линии Cortex-M7, байт. */
#define SDRAM_CACHE_LINE_BYTES 32U

/**
 * @brief Размер sequential теста, байт (2 MB).
 *
 * Больший регион (vs 1 MB в оригинале) — лучше ловит coupling faults.
 */
#define SDRAM_SEQUENTIAL_SIZE 0x00200000UL

/**
 * @brief Размер retention теста, байт (256 KB).
 *
 * Достаточно для проверки refresh timing при 200 мс задержке.
 */
#define SDRAM_RETENTION_SIZE 0x00040000UL

/**
 * @brief Время удержания данных в retention тесте, мс.
 *
 * MT48LC16M16A2: период авто-refresh 64 мс.
 * 200 мс ≈ 3 refresh-периода.
 */
#define SDRAM_RETENTION_DELAY_MS 200U

/** @brief Шаг ожидания в retention тесте (для USB poll), мс. */
#define SDRAM_RETENTION_POLL_STEP_MS 10U

/* ── Локальные типы ────────────────────────────────────────────────────── */

/**
 * @brief Информация о первой ошибке паттерн-прохода.
 *
 * fail_addr == 0 означает отсутствие ошибок.
 */
typedef struct
{
    uint32_t fail_addr; /**< Адрес первой ошибки. */
    uint8_t expected;   /**< Ожидаемое значение. */
    uint8_t got;        /**< Прочитанное значение. */
} sdram_fail_info_t;

/** @brief Функция-генератор одного байта тестового паттерна. */
typedef uint8_t (*pattern_fn_t)(uint32_t offset);

/* ── Состояние модуля ──────────────────────────────────────────────────── */

static bool g_s_ready = false;

/* ── Паттерн-функции ───────────────────────────────────────────────────── */

static uint8_t pattern_walking_ones(uint32_t offset)
{
    return (uint8_t) (1U << (offset % SDRAM_WALKING_ONES_BITS));
}

static uint8_t pattern_walking_ones_inv(uint32_t offset)
{
    return (uint8_t) (~(1U << (offset % SDRAM_WALKING_ONES_BITS)) & 0xFFU);
}

static uint8_t pattern_addr(uint32_t offset)
{
    return (uint8_t) (offset & 0xFFU);
}

static uint8_t pattern_addr_inv(uint32_t offset)
{
    return (uint8_t) (~offset & 0xFFU);
}

/* ── Вспомогательные функции ───────────────────────────────────────────── */

/**
 * @brief Clean + Invalidate кэш для региона от TEST_BASE.
 *
 * Тот же подход что в рабочей версии — SCB_CleanDCache_by_Addr.
 */
static void flush_dcache(uint32_t size)
{
    uint32_t *const P_BASE = (uint32_t *) BSP_SDRAM_TEST_BASE_ADDR;

    SCB_CleanDCache_by_Addr(P_BASE, (int32_t) size);
    SCB_InvalidateDCache_by_Addr(P_BASE, (int32_t) size);
    __DSB();
}

/**
 * @brief Clean + Invalidate одну кэш-линию по произвольному байтовому адресу.
 *
 * Используется в address bus тесте для точечного сброса после каждой записи.
 * Адрес выравнивается на SDRAM_CACHE_LINE_BYTES автоматически.
 *
 * @param[in] byte_addr  Любой байтовый адрес внутри нужной кэш-линии.
 */
static void flush_dcache_line_at(uint32_t byte_addr)
{
    uint32_t *const P_LINE = (uint32_t *) (byte_addr & ~(SDRAM_CACHE_LINE_BYTES - 1U));

    SCB_CleanDCache_by_Addr(P_LINE, (int32_t) SDRAM_CACHE_LINE_BYTES);
    SCB_InvalidateDCache_by_Addr(P_LINE, (int32_t) SDRAM_CACHE_LINE_BYTES);
    __DSB();
}

/**
 * @brief Ожидание с периодическим USB poll.
 */
static void delay_with_poll(uint32_t ms)
{
    const uint32_t START_MS = bsp_tick_get_ms();

    while ((bsp_tick_get_ms() - START_MS) < ms)
    {
        bsp_delay(SDRAM_RETENTION_POLL_STEP_MS);
        bsp_usb_cdc_poll();
    }
}

/**
 * @brief Записать паттерн в тестовый регион.
 */
static void write_pattern(uint32_t size, pattern_fn_t p_fn)
{
    volatile uint8_t *const P_BASE = (volatile uint8_t *) BSP_SDRAM_TEST_BASE_ADDR;

    for (uint32_t i = 0U; i < size; i++)
    {
        if ((i & (SDRAM_USB_POLL_INTERVAL_BYTES - 1U)) == 0U)
        {
            bsp_usb_cdc_poll();
        }
        P_BASE[i] = p_fn(i);
    }
}

/**
 * @brief Верифицировать паттерн. Останавливается на первом несовпадении.
 */
static bool verify_pattern(uint32_t size, pattern_fn_t p_fn, sdram_fail_info_t *p_fail)
{
    volatile const uint8_t *const P_BASE = (volatile const uint8_t *) BSP_SDRAM_TEST_BASE_ADDR;

    for (uint32_t i = 0U; i < size; i++)
    {
        if ((i & (SDRAM_USB_POLL_INTERVAL_BYTES - 1U)) == 0U)
        {
            bsp_usb_cdc_poll();
        }

        const uint8_t EXPECTED = p_fn(i);
        const uint8_t GOT      = P_BASE[i];

        if (GOT != EXPECTED)
        {
            p_fail->fail_addr = BSP_SDRAM_TEST_BASE_ADDR + i;
            p_fail->expected  = EXPECTED;
            p_fail->got       = GOT;
            return false;
        }
    }
    return true;
}

/**
 * @brief Один паттерн-проход: запись → flush → верификация.
 */
static bool run_pass(uint32_t size, pattern_fn_t p_fn, sdram_fail_info_t *p_fail)
{
    write_pattern(size, p_fn);
    flush_dcache(size);
    return verify_pattern(size, p_fn, p_fail);
}

/**
 * @brief Сформировать FAIL-результат.
 */
static test_result_t make_fail_result(const sdram_fail_info_t *p_fail, uint32_t duration_ms)
{
    test_result_t result = {
        .status      = TEST_STATUS_FAIL,
        .duration_ms = duration_ms,
    };
    (void) snprintf(result.detail, TEST_DETAIL_SIZE, "addr=0x%08" PRIX32 " exp=0x%02X got=0x%02X",
                    p_fail->fail_addr, (unsigned int) p_fail->expected, (unsigned int) p_fail->got);
    return result;
}

/* ── Фазы теста ────────────────────────────────────────────────────────── */

/**
 * @brief Фаза 1: шина адреса — 24 бита.
 *
 * Записывает уникальный байт (bit+1) в каждую из 24 позиций степеней двойки.
 * После каждой записи сбрасывает только одну кэш-линию → мгновенно.
 * Максимальное смещение: 2^23 = 8 MB → 0x80A00000 — внутри SDRAM. ✓
 */
static bool run_phase_address_bus(sdram_fail_info_t *p_fail)
{
    volatile uint8_t *const P_BASE = (volatile uint8_t *) BSP_SDRAM_TEST_BASE_ADDR;

    for (uint32_t bit = 0U; bit < SDRAM_ADDR_BUS_BITS; bit++)
    {
        const uint32_t OFFSET = (1UL << bit);
        const uint8_t PATTERN = (uint8_t) (bit + 1U);

        P_BASE[OFFSET] = PATTERN;
        flush_dcache_line_at(BSP_SDRAM_TEST_BASE_ADDR + OFFSET);

        const uint8_t GOT = P_BASE[OFFSET];
        if (GOT != PATTERN)
        {
            p_fail->fail_addr = BSP_SDRAM_TEST_BASE_ADDR + OFFSET;
            p_fail->expected  = PATTERN;
            p_fail->got       = GOT;
            return false;
        }
    }
    return true;
}

/**
 * @brief Фаза 2: шина данных — 64 KB.
 *
 * Walking ones + инверсия. Ловит stuck-at faults на всех битах.
 */
static bool run_phase_data_bus(sdram_fail_info_t *p_fail)
{
    return run_pass(BSP_SDRAM_TEST_FAST_SIZE, pattern_walking_ones, p_fail) &&
           run_pass(BSP_SDRAM_TEST_FAST_SIZE, pattern_walking_ones_inv, p_fail);
}

/**
 * @brief Фаза 3: sequential integrity — 2 MB.
 *
 * Address pattern + инверсия. Ловит coupling faults между соседними ячейками.
 */
static bool run_phase_sequential(sdram_fail_info_t *p_fail)
{
    return run_pass(SDRAM_SEQUENTIAL_SIZE, pattern_addr, p_fail) &&
           run_pass(SDRAM_SEQUENTIAL_SIZE, pattern_addr_inv, p_fail);
}

/**
 * @brief Фаза 4: retention — 256 KB, 200 мс.
 *
 * Запись → flush → ожидание → верификация.
 * Ловит refresh timing failures.
 */
static bool run_phase_retention(sdram_fail_info_t *p_fail)
{
    write_pattern(SDRAM_RETENTION_SIZE, pattern_addr);
    flush_dcache(SDRAM_RETENTION_SIZE);
    delay_with_poll(SDRAM_RETENTION_DELAY_MS);
    return verify_pattern(SDRAM_RETENTION_SIZE, pattern_addr, p_fail);
}

/* ── Реализация тест-модуля ────────────────────────────────────────────── */

static void sdram_test_init(void)
{
    g_s_ready = (bsp_sdram_init() == BSP_OK);
}

static test_result_t sdram_test_run(void)
{
    if (!g_s_ready)
    {
        test_result_t result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U };
        (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", "SEMC not ready — DCD failed?");
        return result;
    }

    const uint32_t START_MS = bsp_tick_get_ms();
    sdram_fail_info_t fail  = { 0U, 0U, 0U };

    bool is_ok = run_phase_address_bus(&fail);
    if (is_ok)
    {
        is_ok = run_phase_data_bus(&fail);
    }
    if (is_ok)
    {
        is_ok = run_phase_sequential(&fail);
    }
    if (is_ok)
    {
        is_ok = run_phase_retention(&fail);
    }

    const uint32_t DURATION_MS = bsp_tick_get_ms() - START_MS;

    if (!is_ok)
    {
        return make_fail_result(&fail, DURATION_MS);
    }

    return (test_result_t){
        .status      = TEST_STATUS_PASS,
        .duration_ms = DURATION_MS,
        .detail      = { 0 },
    };
}

static void sdram_test_deinit(void)
{
    g_s_ready = false;
}

/* ── Дескриптор модуля ─────────────────────────────────────────────────── */

const test_module_t K_TEST_SDRAM = {
    .id                 = "sdram",
    .name               = "SDRAM 32 MB",
    .critical           = true,
    .requires_hil       = false,
    .pre_confirm_prompt = NULL,
    .init               = sdram_test_init,
    .run                = sdram_test_run,
    .deinit             = sdram_test_deinit,
};