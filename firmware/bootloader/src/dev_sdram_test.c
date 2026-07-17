/**
 * @file  dev_sdram_test.c
 * @brief [DEV-ONLY] Реализация глубокого теста SDRAM (см. dev_sdram_test.h).
 *
 * Фазы 1:1 портированы из firmware/test/src/tests/test_sdram.c (тот же
 * алгоритм, то же покрытие) — не переизобретаются, чтобы результат был
 * сопоставим с уже доверенным тестом DCD-пути. Отличия от оригинала:
 *   - sdram_test_init() там предполагал DCD; здесь сама зовёт
 *     bsp_sdram_configure() — это и есть предмет проверки.
 *   - test_module_t/test_result_t (инфраструктура тест-раннера firmware_test)
 *     не используются — bootloader её не имеет; результат каждой фазы уходит
 *     отдельным CDC-событием по ходу прогона, не одним отчётом в конце.
 *   - добавлено кормление watchdog (bsp_wdog_refresh()) на той же частоте,
 *     что и опрос CDC — без него более медленный прогон рисковал бы не
 *     пережить таймаут WDOG (10 с, main.c) и словить сброс посреди фазы
 *     sequential (по факту прогон ~4 с, запас большой — см. dev_sdram_test.h).
 */

#include "dev_sdram_test.h"

#include "bsp/sdram.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "bsp/wdog.h"
#include "fsl_common.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Константы (те же значения, что в firmware_test/test_sdram.c) ───────── */

/** @brief Интервал вызова pump() при записи/чтении, байт. */
#define SDRAM_USB_POLL_INTERVAL_BYTES 0x1000U

/** @brief Ширина walking ones паттерна, бит. */
#define SDRAM_WALKING_ONES_BITS 8U

/** @brief Количество адресных бит теста address_bus (13 row+9 col+2 bank). */
#define SDRAM_ADDR_BUS_BITS 24U

/** @brief Размер кэш-линии Cortex-M7, байт. */
#define SDRAM_CACHE_LINE_BYTES 32U

/** @brief Размер фазы sequential, байт (2 MB). */
#define SDRAM_SEQUENTIAL_SIZE 0x00200000UL

/** @brief Размер фазы retention, байт (256 KB). */
#define SDRAM_RETENTION_SIZE 0x00040000UL

/** @brief Задержка фазы retention, мс (MT48LC16M16A2: авто-refresh 64 мс, ≈3 периода). */
#define SDRAM_RETENTION_DELAY_MS 200U

/** @brief Шаг ожидания в фазе retention (для pump()), мс. */
#define SDRAM_RETENTION_POLL_STEP_MS 10U

/* ── Локальные типы ────────────────────────────────────────────────────── */

/** @brief Информация о первой ошибке паттерн-прохода. fail_addr==0 — нет ошибки. */
typedef struct
{
    uint32_t fail_addr;
    uint8_t expected;
    uint8_t got;
} sdram_fail_info_t;

typedef uint8_t (*pattern_fn_t)(uint32_t offset);

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

/** @brief Опрос CDC + кормление watchdog. Вызывать не реже раза в ~4 KB. */
static void pump(void)
{
    bsp_usb_cdc_poll();
    bsp_wdog_refresh();
}

static void flush_dcache(uint32_t size)
{
    uint32_t *const P_BASE = (uint32_t *) BSP_SDRAM_TEST_BASE_ADDR;

    SCB_CleanDCache_by_Addr(P_BASE, (int32_t) size);
    SCB_InvalidateDCache_by_Addr(P_BASE, (int32_t) size);
    __DSB();
}

/** @brief Сброс одной кэш-линии по произвольному байтовому адресу (для address_bus). */
static void flush_dcache_line_at(uint32_t byte_addr)
{
    uint32_t *const P_LINE = (uint32_t *) (byte_addr & ~(SDRAM_CACHE_LINE_BYTES - 1U));

    SCB_CleanDCache_by_Addr(P_LINE, (int32_t) SDRAM_CACHE_LINE_BYTES);
    SCB_InvalidateDCache_by_Addr(P_LINE, (int32_t) SDRAM_CACHE_LINE_BYTES);
    __DSB();
}

static void delay_with_pump(uint32_t ms)
{
    const uint32_t START_MS = bsp_tick_get_ms();

    while ((bsp_tick_get_ms() - START_MS) < ms)
    {
        bsp_delay(SDRAM_RETENTION_POLL_STEP_MS);
        pump();
    }
}

static void write_pattern(uint32_t size, pattern_fn_t p_fn)
{
    volatile uint8_t *const P_BASE = (volatile uint8_t *) BSP_SDRAM_TEST_BASE_ADDR;

    for (uint32_t i = 0U; i < size; i++)
    {
        if ((i & (SDRAM_USB_POLL_INTERVAL_BYTES - 1U)) == 0U)
        {
            pump();
        }
        P_BASE[i] = p_fn(i);
    }
}

/** @brief Верифицировать паттерн. Останавливается на первом несовпадении. */
static bool verify_pattern(uint32_t size, pattern_fn_t p_fn, sdram_fail_info_t *p_fail)
{
    volatile const uint8_t *const P_BASE = (volatile const uint8_t *) BSP_SDRAM_TEST_BASE_ADDR;

    for (uint32_t i = 0U; i < size; i++)
    {
        if ((i & (SDRAM_USB_POLL_INTERVAL_BYTES - 1U)) == 0U)
        {
            pump();
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

/** @brief Один паттерн-проход: запись → flush → верификация. */
static bool run_pass(uint32_t size, pattern_fn_t p_fn, sdram_fail_info_t *p_fail)
{
    write_pattern(size, p_fn);
    flush_dcache(size);
    return verify_pattern(size, p_fn, p_fail);
}

/* ── Фазы теста (см. dev_sdram_test.h) ───────────────────────────────────── */

/** @brief Фаза address_bus: 24 адресных бита, точечный флаш на каждую запись. */
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

/** @brief Фаза data_bus: walking ones + инверсия, 64 KB. */
static bool run_phase_data_bus(sdram_fail_info_t *p_fail)
{
    return run_pass(BSP_SDRAM_TEST_FAST_SIZE, pattern_walking_ones, p_fail) &&
           run_pass(BSP_SDRAM_TEST_FAST_SIZE, pattern_walking_ones_inv, p_fail);
}

/** @brief Фаза sequential: address pattern + инверсия, 2 MB. */
static bool run_phase_sequential(sdram_fail_info_t *p_fail)
{
    return run_pass(SDRAM_SEQUENTIAL_SIZE, pattern_addr, p_fail) &&
           run_pass(SDRAM_SEQUENTIAL_SIZE, pattern_addr_inv, p_fail);
}

/** @brief Фаза retention: запись → flush → 200 мс → verify, 256 KB. */
static bool run_phase_retention(sdram_fail_info_t *p_fail)
{
    write_pattern(SDRAM_RETENTION_SIZE, pattern_addr);
    flush_dcache(SDRAM_RETENTION_SIZE);
    delay_with_pump(SDRAM_RETENTION_DELAY_MS);
    return verify_pattern(SDRAM_RETENTION_SIZE, pattern_addr, p_fail);
}

/* ── Оркестрация ──────────────────────────────────────────────────────── */

static void report_phase(const char *p_name, bool pass, uint32_t start_ms, const sdram_fail_info_t *p_fail)
{
    const uint32_t DURATION_MS = bsp_tick_get_ms() - start_ms;
    const uint32_t FAIL_ADDR   = (p_fail != NULL) ? p_fail->fail_addr : 0U;
    const uint8_t EXPECTED     = (p_fail != NULL) ? p_fail->expected : 0U;
    const uint8_t GOT          = (p_fail != NULL) ? p_fail->got : 0U;

    protocol_send_sdram_test_phase(p_name, pass, DURATION_MS, FAIL_ADDR, EXPECTED, GOT);
}

void dev_sdram_test_run(void)
{
    const uint32_t CONFIGURE_START_MS = bsp_tick_get_ms();

    const bool CONFIGURED = (bsp_sdram_configure() == BSP_OK) && (bsp_sdram_init() == BSP_OK);
    report_phase("configure", CONFIGURED, CONFIGURE_START_MS, NULL);
    if (!CONFIGURED)
    {
        return;
    }

    const uint32_t OVERALL_START_MS = bsp_tick_get_ms();
    sdram_fail_info_t fail          = { 0U, 0U, 0U };
    bool is_ok;
    uint32_t phase_start_ms;

    phase_start_ms = bsp_tick_get_ms();
    is_ok          = run_phase_address_bus(&fail);
    report_phase("address_bus", is_ok, phase_start_ms, is_ok ? NULL : &fail);

    if (is_ok)
    {
        phase_start_ms = bsp_tick_get_ms();
        is_ok          = run_phase_data_bus(&fail);
        report_phase("data_bus", is_ok, phase_start_ms, is_ok ? NULL : &fail);
    }

    if (is_ok)
    {
        phase_start_ms = bsp_tick_get_ms();
        is_ok          = run_phase_sequential(&fail);
        report_phase("sequential", is_ok, phase_start_ms, is_ok ? NULL : &fail);
    }

    if (is_ok)
    {
        phase_start_ms = bsp_tick_get_ms();
        is_ok          = run_phase_retention(&fail);
        report_phase("retention", is_ok, phase_start_ms, is_ok ? NULL : &fail);
    }

    report_phase("summary", is_ok, OVERALL_START_MS, NULL);
}
