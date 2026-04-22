/**
 * @file  test_qspi.c
 * @brief Тест-модуль firmware_test: QSPI Flash W25Q64/128/256/512.
 *
 * Четыре шага:
 *
 *   Шаг 1 — JEDEC ID (~1 мс)
 *     Команда 0x9F. Проверяем manufacturer=0xEF (Winbond) и что capacity
 *     byte принадлежит известному чипу. По результату вычисляем адрес
 *     тестового сектора (последний 4KB в Flash) и размер чипа.
 *
 *   Шаг 2 — Erase + Verify (~500 мс)
 *     Стираем последний сектор. Читаем 256 байт, все должны быть 0xFF.
 *
 *   Шаг 3 — Write + Read + Compare (~200 мс)
 *     Паттерн: byte[i] = i & 0xFF, 256 байт. Пишем на первую страницу
 *     тестового сектора. Читаем и сравниваем побайтово.
 *
 *   Шаг 4 — Address range (только W25Q256/512, ~100 мс)
 *     Детектирует алиасирование адресов при неработающих dedicated 4-byte
 *     opcodes. Пишем 0xAA в anchor сектор (0x00FFF000), читаем test_addr.
 *     Если читаем 0xAA — test_addr физически алиасирует на anchor_addr —
 *     IP-команды работают в 3-byte режиме вместо 4-byte → FAIL.
 *
 * Тестовый сектор = bsp_qspi_flash_size() - BSP_QSPI_SECTOR_SIZE.
 * Адрес вычисляется после Шага 1 динамически — поддерживает все чипы.
 */

#include "bsp/qspi_flash.h"
#include "bsp/usb_cdc.h"
#include "test_module.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Число байт верификации после erase (первая страница сектора). */
#define QSPI_ERASE_VERIFY_LEN BSP_QSPI_PAGE_SIZE

/** @brief Ожидаемый байт в стёртом секторе. */
#define QSPI_ERASED_BYTE 0xFFU

/**
 * @brief Граница 16 MB — максимум 24-bit адресного пространства.
 *
 * Для чипов с flash_size > 16 MB (W25Q256/512) проверяется алиасирование.
 */
#define QSPI_ADDR_16MB 0x01000000UL

/**
 * @brief Адрес anchor-сектора для проверки алиасирования.
 *
 * При 24-bit алиасировании:
 *   W25Q256: 0x1FFF000 & 0xFFFFFF = 0xFFF000 ← этот адрес
 *   W25Q512: 0x3FFF000 & 0xFFFFFF = 0xFFF000 ← тот же адрес
 *
 * Если dedicated 4-byte opcodes не работают, запись/чтение last-sector
 * фактически попадает на этот адрес.
 */
#define QSPI_ALIAS_ANCHOR_ADDR 0x00FFF000UL

/** @brief Паттерн anchor-страницы. Отличается от основного (i & 0xFF). */
#define QSPI_ANCHOR_PATTERN 0xAAU

/* ── Буферы ─────────────────────────────────────────────────────────────── */

static uint8_t g_s_write_buf[BSP_QSPI_PAGE_SIZE];  /**< Данные для записи.    */
static uint8_t g_s_read_buf[BSP_QSPI_PAGE_SIZE];   /**< Данные после чтения.  */
static uint8_t g_s_anchor_buf[BSP_QSPI_PAGE_SIZE]; /**< Anchor для Шага 4.    */

/* ── Состояние модуля ───────────────────────────────────────────────────── */

static bool g_s_ready = false;

/* ── Вспомогательные функции ────────────────────────────────────────────── */

static test_result_t make_fail(const char *p_detail)
{
    test_result_t result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U };
    (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", p_detail);
    return result;
}

/* ── Шаг 1: JEDEC ID ────────────────────────────────────────────────────── */

/**
 * @brief Читает JEDEC ID, верифицирует производителя и chip capacity.
 *
 * @param[out] p_test_addr   Адрес тестового сектора (последний 4KB).
 * @param[out] p_result      FAIL-результат при ошибке.
 * @return true при успехе.
 */
static bool step_jedec(uint32_t *p_test_addr, test_result_t *p_result)
{
    bsp_qspi_jedec_t jedec = { .manufacturer_id = 0U, .device_id = 0U };

    bsp_usb_cdc_poll();

    if (bsp_qspi_read_jedec_id(&jedec) != BSP_OK)
    {
        *p_result = make_fail("JEDEC: read failed");
        return false;
    }

    if (jedec.manufacturer_id != BSP_QSPI_MFR_WINBOND)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "JEDEC: mfr=0x%02X exp=0x%02X",
                        (unsigned int) jedec.manufacturer_id, (unsigned int) BSP_QSPI_MFR_WINBOND);
        *p_result = make_fail(detail);
        return false;
    }

    const uint8_t CAP = (uint8_t) (jedec.device_id & 0xFFU);

    switch (CAP)
    {
    case BSP_QSPI_CAP_64MBIT:
    case BSP_QSPI_CAP_128MBIT:
    case BSP_QSPI_CAP_256MBIT:
    case BSP_QSPI_CAP_512MBIT:
        break;
    default:
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "JEDEC: unknown cap=0x%02X dev=0x%04X",
                        (unsigned int) CAP, (unsigned int) jedec.device_id);
        *p_result = make_fail(detail);
        return false;
    }
    }

    *p_test_addr = bsp_qspi_flash_size() - BSP_QSPI_SECTOR_SIZE;
    return true;
}

/* ── Шаг 2: Erase + Verify ──────────────────────────────────────────────── */

/**
 * @brief Стирает тестовый сектор и верифицирует (все байты = 0xFF).
 *
 * @param[in]  test_addr  Адрес тестового сектора.
 * @param[out] p_result   FAIL-результат при ошибке.
 * @return true при успехе.
 */
static bool step_erase(uint32_t test_addr, test_result_t *p_result)
{
    bsp_usb_cdc_poll();

    if (bsp_qspi_erase_sector(test_addr) != BSP_OK)
    {
        *p_result = make_fail("erase: sector erase failed");
        return false;
    }

    bsp_usb_cdc_poll();

    (void) memset(g_s_read_buf, 0x00, sizeof(g_s_read_buf));

    if (bsp_qspi_read(test_addr, g_s_read_buf, QSPI_ERASE_VERIFY_LEN) != BSP_OK)
    {
        *p_result = make_fail("erase verify: read failed");
        return false;
    }

    bsp_usb_cdc_poll();

    for (uint32_t i = 0U; i < QSPI_ERASE_VERIFY_LEN; i++)
    {
        if (g_s_read_buf[i] != QSPI_ERASED_BYTE)
        {
            char detail[TEST_DETAIL_SIZE];
            (void) snprintf(detail, sizeof(detail),
                            "erase verify failed at 0x%08" PRIX32 " got=0x%02X", test_addr + i,
                            (unsigned int) g_s_read_buf[i]);
            *p_result = make_fail(detail);
            return false;
        }
    }
    return true;
}

/* ── Шаг 3: Write + Read + Compare ─────────────────────────────────────── */

/**
 * @brief Пишет паттерн (i & 0xFF) и верифицирует побайтово.
 *
 * @param[in]  test_addr  Адрес тестового сектора.
 * @param[out] p_result   FAIL-результат при ошибке.
 * @return true при успехе.
 */
static bool step_write_read(uint32_t test_addr, test_result_t *p_result)
{
    for (uint32_t i = 0U; i < BSP_QSPI_PAGE_SIZE; i++)
    {
        g_s_write_buf[i] = (uint8_t) (i & 0xFFU);
    }

    bsp_usb_cdc_poll();

    if (bsp_qspi_write_page(test_addr, g_s_write_buf) != BSP_OK)
    {
        *p_result = make_fail("rw: page write failed");
        return false;
    }

    bsp_usb_cdc_poll();

    (void) memset(g_s_read_buf, 0x00, sizeof(g_s_read_buf));

    if (bsp_qspi_read(test_addr, g_s_read_buf, BSP_QSPI_PAGE_SIZE) != BSP_OK)
    {
        *p_result = make_fail("rw: page read failed");
        return false;
    }

    bsp_usb_cdc_poll();

    for (uint32_t i = 0U; i < BSP_QSPI_PAGE_SIZE; i++)
    {
        if (g_s_read_buf[i] != g_s_write_buf[i])
        {
            char detail[TEST_DETAIL_SIZE];
            (void) snprintf(detail, sizeof(detail),
                            "rw mismatch at 0x%08" PRIX32 " exp=0x%02X got=0x%02X", test_addr + i,
                            (unsigned int) g_s_write_buf[i], (unsigned int) g_s_read_buf[i]);
            *p_result = make_fail(detail);
            return false;
        }
    }
    return true;
}

/* ── Шаг 4: Address range ───────────────────────────────────────────────── */

/**
 * @brief Верифицирует отсутствие адресного алиасирования (W25Q256/512).
 *
 * После step_write_read: test_addr содержит паттерн i & 0xFF.
 * Записываем 0xAA в anchor_addr (0x00FFF000). Если test_addr физически
 * совпадает с anchor_addr (24-bit wrap), чтение test_addr вернёт 0xAA.
 *
 * Для W25Q64/128 (flash_size ≤ 16 MB): шаг пропускается.
 *
 * @param[in]  test_addr   Адрес тестового сектора.
 * @param[in]  flash_size  Полный размер Flash.
 * @param[out] p_result    FAIL-результат при ошибке.
 * @return true если адресация верна или тест неприменим.
 */
static bool step_address_range(uint32_t test_addr, uint32_t flash_size, test_result_t *p_result)
{
    if (flash_size <= QSPI_ADDR_16MB)
    {
        return true; /* W25Q64/128: 24-bit покрывает весь чип */
    }

    /* Шаг A: стереть anchor сектор */
    bsp_usb_cdc_poll();
    if (bsp_qspi_erase_sector(QSPI_ALIAS_ANCHOR_ADDR) != BSP_OK)
    {
        *p_result = make_fail("addr range: anchor erase failed");
        return false;
    }
    bsp_usb_cdc_poll();

    /* Шаг B: записать 0xAA на anchor_addr */
    (void) memset(g_s_anchor_buf, QSPI_ANCHOR_PATTERN, sizeof(g_s_anchor_buf));
    if (bsp_qspi_write_page(QSPI_ALIAS_ANCHOR_ADDR, g_s_anchor_buf) != BSP_OK)
    {
        *p_result = make_fail("addr range: anchor write failed");
        (void) bsp_qspi_erase_sector(QSPI_ALIAS_ANCHOR_ADDR);
        return false;
    }
    bsp_usb_cdc_poll();

    /* Шаг C: прочитать test_addr — должен содержать паттерн i & 0xFF */
    (void) memset(g_s_read_buf, 0x00, sizeof(g_s_read_buf));
    if (bsp_qspi_read(test_addr, g_s_read_buf, BSP_QSPI_PAGE_SIZE) != BSP_OK)
    {
        (void) bsp_qspi_erase_sector(QSPI_ALIAS_ANCHOR_ADDR);
        *p_result = make_fail("addr range: high addr read failed");
        return false;
    }
    bsp_usb_cdc_poll();

    /* Шаг D: верификация */
    bool ok = true;
    char detail[TEST_DETAIL_SIZE];
    detail[0U] = '\0';

    for (uint32_t i = 0U; i < BSP_QSPI_PAGE_SIZE; i++)
    {
        const uint8_t EXPECTED = (uint8_t) (i & 0xFFU);
        const uint8_t GOT      = g_s_read_buf[i];

        if (GOT != EXPECTED)
        {
            if (GOT == QSPI_ANCHOR_PATTERN)
            {
                (void) snprintf(detail, sizeof(detail),
                                "addr alias: 0x%08" PRIX32 " mirrors 0x%08" PRIX32 " (3-byte wrap)",
                                test_addr, QSPI_ALIAS_ANCHOR_ADDR);
            }
            else
            {
                (void) snprintf(detail, sizeof(detail),
                                "addr range mismatch at 0x%08" PRIX32 " exp=0x%02X got=0x%02X",
                                test_addr + i, (unsigned int) EXPECTED, (unsigned int) GOT);
            }
            ok = false;
            break;
        }
    }

    /* Шаг E: cleanup anchor */
    bsp_usb_cdc_poll();
    (void) bsp_qspi_erase_sector(QSPI_ALIAS_ANCHOR_ADDR);
    bsp_usb_cdc_poll();

    if (!ok)
    {
        *p_result = make_fail(detail);
        return false;
    }
    return true;
}

/* ── Реализация тест-модуля ─────────────────────────────────────────────── */

static void qspi_test_init(void)
{
    g_s_ready = (bsp_qspi_init() == BSP_OK);
}

static test_result_t qspi_test_run(void)
{
    if (!g_s_ready)
    {
        return make_fail("init failed — FlexSPI or unknown chip");
    }

    test_result_t fail_result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U };
    uint32_t test_addr        = 0U;

    if (!step_jedec(&test_addr, &fail_result))
    {
        return fail_result;
    }
    if (!step_erase(test_addr, &fail_result))
    {
        return fail_result;
    }
    if (!step_write_read(test_addr, &fail_result))
    {
        return fail_result;
    }
    if (!step_address_range(test_addr, bsp_qspi_flash_size(), &fail_result))
    {
        return fail_result;
    }

    return (test_result_t){
        .status      = TEST_STATUS_PASS,
        .duration_ms = 0U,
        .detail      = { 0 },
    };
}

static void qspi_test_deinit(void)
{
    g_s_ready = false;
}

/* ── Дескриптор модуля ──────────────────────────────────────────────────── */

const test_module_t K_TEST_QSPI = {
    .id                 = "qspi",
    .name               = "QSPI Flash W25Qxx",
    .critical           = true,
    .requires_hil       = false,
    .pre_confirm_prompt = NULL,
    .init               = qspi_test_init,
    .run                = qspi_test_run,
    .deinit             = qspi_test_deinit,
};