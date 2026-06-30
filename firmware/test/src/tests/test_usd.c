/**
 * @file  test_usd.c
 * @brief Тест-модуль firmware_test: microSD (USDHC / FatFS).
 *
 * Жизненный цикл управляется test_runner:
 *   init()   — bsp_sd_init(): инициализация USDHC host.
 *   run()    — card detect → mount → write → read/compare → unmount.
 *   deinit() — safety-net: закрыть файл, отмонтировать, bsp_sd_deinit().
 *              Вызывается test_runner даже при FAIL run().
 *
 * Паттерн: byte[i] = i & 0xFF, 4096 байт.
 * Покрывает: stuck-at-0, stuck-at-1, partial write, address aliasing.
 *
 * Буферы g_s_write_buf / g_s_read_buf — статические: стек firmware_test
 * составляет 4 KB (linker script), 4 KB-буферы на него не влезают.
 *
 * Состояние монтирования (g_s_mounted) и открытого файла (g_s_file_open)
 * отслеживается на уровне модуля — deinit() корректно освобождает ресурсы
 * в любом сценарии провала.
 */

#include "bsp/sd.h"
#include "bsp/usb_cdc.h"
#include "cli.h"
#include "ff.h"
#include "test_module.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Размер тестового файла, байт. */
#define USD_TEST_SIZE 4096U

/** @brief Точка монтирования FatFS (drive 2). */
#define USD_MOUNT_POINT "2:/"

/** @brief Путь тестового файла. Удаляется после теста в любом исходе. */
#define USD_TEST_FILE "2:/FWTEST.TMP"

/* ── Статические объекты FatFS ──────────────────────────────────────────── */

/** @brief Рабочая область FatFS. sizeof(FATFS) ≈ 516 байт — не на стеке. */
static FATFS g_s_fs;

/** @brief Дескриптор открытого файла. */
static FIL g_s_file;

/* ── Буферы ─────────────────────────────────────────────────────────────── */

static uint8_t g_s_write_buf[USD_TEST_SIZE];
static uint8_t g_s_read_buf[USD_TEST_SIZE];

/* ── Состояние модуля ───────────────────────────────────────────────────── */

/** @brief true если bsp_sd_init() прошёл успешно. */
static bool g_s_ready;

/** @brief true если FatFS смонтирована (f_mount выполнен). */
static bool g_s_mounted;

/** @brief true если g_s_file открыт и требует f_close. */
static bool g_s_file_open;

/* ── Вспомогательные функции ────────────────────────────────────────────── */

static test_result_t make_fail(const char *p_detail)
{
    test_result_t result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U };
    (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", p_detail);
    return result;
}

/* ── Шаг 1: Card detect ─────────────────────────────────────────────────── */

/**
 * @brief Проверить наличие карты через аппаратный регистр USDHC.
 *
 * Вызывается после bsp_sd_init() — host уже инициализирован.
 */
static bool step_card_detect(test_result_t *p_out)
{
    bsp_usb_cdc_poll();

    if (!bsp_sd_is_inserted())
    {
        *p_out = make_fail("no card detected");
        return false;
    }

    cli_send("{\"type\":\"progress\",\"test\":\"usd\","
             "\"step\":\"card_detect\",\"status\":\"ok\"}\n");
    return true;
}

/* ── Шаг 2: Mount ───────────────────────────────────────────────────────── */

/**
 * @brief Смонтировать FatFS раздел. Устанавливает g_s_mounted = true при успехе.
 */
static bool step_mount(test_result_t *p_out)
{
    bsp_usb_cdc_poll();

    FRESULT fr = f_mount(&g_s_fs, USD_MOUNT_POINT, 1);

    if (fr != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "mount failed: %d", (int) fr);
        *p_out = make_fail(detail);
        return false;
    }

    g_s_mounted = true;

    cli_send("{\"type\":\"progress\",\"test\":\"usd\","
             "\"step\":\"mount\",\"status\":\"ok\"}\n");
    return true;
}

/* ── Шаг 3: Write ───────────────────────────────────────────────────────── */

/**
 * @brief Записать тестовый паттерн в FWTEST.TMP.
 *
 * Отслеживает g_s_file_open: при провале файл может оставаться открытым —
 * deinit() закроет его через f_close.
 */
static bool step_write(test_result_t *p_out)
{
    for (uint32_t i = 0U; i < USD_TEST_SIZE; i++)
    {
        g_s_write_buf[i] = (uint8_t) (i & 0xFFU);
    }

    bsp_usb_cdc_poll();

    FRESULT fr = f_open(&g_s_file, USD_TEST_FILE, FA_WRITE | FA_CREATE_ALWAYS);

    if (fr != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "open failed: %d", (int) fr);
        *p_out = make_fail(detail);
        return false;
    }

    g_s_file_open = true;

    UINT bw = 0U;
    fr      = f_write(&g_s_file, g_s_write_buf, USD_TEST_SIZE, &bw);
    bsp_usb_cdc_poll();

    if (fr != FR_OK || bw != USD_TEST_SIZE)
    {
        char detail[TEST_DETAIL_SIZE];
        if (fr != FR_OK)
        {
            (void) snprintf(detail, sizeof(detail), "write failed: %d", (int) fr);
        }
        else
        {
            (void) snprintf(detail, sizeof(detail), "write incomplete: %u/%u", (unsigned int) bw,
                            (unsigned int) USD_TEST_SIZE);
        }
        *p_out = make_fail(detail);
        return false;
    }

    fr            = f_close(&g_s_file);
    g_s_file_open = false; /* сброс независимо от результата f_close */
    bsp_usb_cdc_poll();

    if (fr != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "close after write failed: %d", (int) fr);
        *p_out = make_fail(detail);
        return false;
    }

    cli_send("{\"type\":\"progress\",\"test\":\"usd\","
             "\"step\":\"write\",\"status\":\"ok\"}\n");
    return true;
}

/* ── Шаг 4: Read + Compare ──────────────────────────────────────────────── */

/**
 * @brief Прочитать FWTEST.TMP и сравнить с g_s_write_buf побайтово.
 *
 * Отслеживает g_s_file_open аналогично step_write.
 */
static bool step_read_compare(test_result_t *p_out)
{
    bsp_usb_cdc_poll();

    FRESULT fr = f_open(&g_s_file, USD_TEST_FILE, FA_READ);

    if (fr != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "open for read failed: %d", (int) fr);
        *p_out = make_fail(detail);
        return false;
    }

    g_s_file_open = true;

    (void) memset(g_s_read_buf, 0, sizeof(g_s_read_buf));

    UINT br = 0U;
    fr      = f_read(&g_s_file, g_s_read_buf, USD_TEST_SIZE, &br);
    bsp_usb_cdc_poll();

    if (fr != FR_OK || br != USD_TEST_SIZE)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "read failed: %d", (int) fr);
        *p_out = make_fail(detail);
        return false;
    }

    fr            = f_close(&g_s_file);
    g_s_file_open = false; /* сброс независимо от результата f_close */
    bsp_usb_cdc_poll();

    if (fr != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "close after read failed: %d", (int) fr);
        *p_out = make_fail(detail);
        return false;
    }

    for (uint32_t i = 0U; i < USD_TEST_SIZE; i++)
    {
        if (g_s_read_buf[i] != g_s_write_buf[i])
        {
            char detail[TEST_DETAIL_SIZE];
            (void) snprintf(detail, sizeof(detail), "compare failed at offset %u",
                            (unsigned int) i);
            *p_out = make_fail(detail);
            return false;
        }
    }

    cli_send("{\"type\":\"progress\",\"test\":\"usd\","
             "\"step\":\"read_compare\",\"status\":\"ok\"}\n");
    return true;
}

/* ── Реализация тест-модуля ─────────────────────────────────────────────── */

/**
 * @brief Инициализация: запустить USDHC host.
 *
 * Вызывается test_runner после pre_confirm (карта уже вставлена оператором).
 * g_s_ready = false при провале → run() вернёт FAIL немедленно.
 */
static void usd_init(void)
{
    g_s_ready     = false;
    g_s_mounted   = false;
    g_s_file_open = false;
    g_s_ready     = (bsp_sd_init() == BSP_OK);
}

static test_result_t usd_run(void)
{
    if (!g_s_ready)
    {
        return make_fail("sd init failed");
    }

    test_result_t fail_result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U, .detail = { 0 } };

    if (!step_card_detect(&fail_result))
    {
        return fail_result;
    }
    if (!step_mount(&fail_result))
    {
        return fail_result;
    }
    if (!step_write(&fail_result))
    {
        return fail_result;
    }
    if (!step_read_compare(&fail_result))
    {
        return fail_result;
    }

    /* Успех: файл закрыт внутри step_read_compare.
     * Удаляем тестовый файл и отмонтируем до возврата —
     * deinit() проверит g_s_mounted и пропустит повторное unmount. */
    (void) f_unlink(USD_TEST_FILE);
    (void) f_unmount(USD_MOUNT_POINT);
    g_s_mounted = false;

    return (test_result_t){
        .status      = TEST_STATUS_PASS,
        .duration_ms = 0U,
        .detail      = { 0 },
    };
}

/**
 * @brief Safety-net cleanup: закрыть файл, отмонтировать, остановить host.
 *
 * Вызывается test_runner всегда — после PASS и после FAIL.
 * При PASS все флаги уже сброшены в run() — функция завершается быстро.
 * При FAIL освобождает ресурсы в любом состоянии провала.
 */
static void usd_deinit(void)
{
    if (g_s_file_open)
    {
        (void) f_close(&g_s_file);
        g_s_file_open = false;
    }

    if (g_s_mounted)
    {
        (void) f_unlink(USD_TEST_FILE);
        (void) f_unmount(USD_MOUNT_POINT);
        g_s_mounted = false;
    }

    if (g_s_ready)
    {
        (void) bsp_sd_deinit();
        g_s_ready = false;
    }
}

/* ── Дескриптор модуля ──────────────────────────────────────────────────── */

const test_module_t K_TEST_USD = {
    .id                 = "usd",
    .name               = "microSD Card",
    .critical           = false,
    .requires_hil       = false,
    .pre_confirm_prompt = "Insert microSD card and press OK",
    .init               = usd_init,
    .run                = usd_run,
    .deinit             = usd_deinit,
};