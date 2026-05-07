/**
 * @file  test_usd.c
 * @brief Тест-модуль firmware_test: microSD (USDHC / FatFS).
 *
 * Пять шагов:
 *
 *   Шаг 1 — Card detect (~1 мс)
 *     bsp_sd_is_inserted(). Карта должна быть вставлена оператором —
 *     pre_confirm_prompt отрабатывает до вызова run() на уровне test_runner.
 *
 *   Шаг 2 — SD init (~200 мс)
 *     bsp_sd_init(): инициализация USDHC host и подключение к карте.
 *
 *   Шаг 3 — Mount (~200 мс)
 *     f_mount(&g_s_fs, "2:/", 1): монтирование FatFS раздела.
 *
 *   Шаг 4 — Write (~500 мс)
 *     Паттерн byte[i] = i & 0xFF, 4096 байт.
 *     Создаём FWTEST.TMP, записываем, закрываем.
 *
 *   Шаг 5 — Read + Compare (~200 мс)
 *     Открываем FWTEST.TMP на чтение, читаем 4096 байт, сравниваем побайтово.
 *
 * Cleanup (unlink + unmount + deinit) выполняется во всех исходах через
 * usd_cleanup(). Тестовый файл FWTEST.TMP удаляется независимо от результата.
 *
 * @note  Буферы 4 KB + FatFS-объекты — статические. На стек не ложатся:
 *        стек firmware_test = 4 KB (linker script ram.ld).
 */

#include "bsp/sd.h"
#include "bsp/usb_cdc.h"
#include "cli.h"
#include "ff.h"
#include "test_module.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Размер тестового файла, байт (покрывает stuck-at и partial write). */
#define USD_TEST_SIZE 4096U

/** @brief Точка монтирования FatFS (drive 2, задан в firmware_test_fatfs). */
#define USD_MOUNT_POINT "2:/"

/** @brief Путь временного тестового файла. */
#define USD_TEST_FILE "2:/FWTEST.TMP"

/* ── Статические объекты FatFS и буферы ────────────────────────────────── */

/** @brief Рабочая область FatFS. Не на стеке — sizeof(FATFS) ≈ 516 байт. */
static FATFS g_s_fs;

/** @brief Дескриптор открытого файла. */
static FIL g_s_file;

/** @brief Буфер тестового паттерна (запись). */
static uint8_t g_s_write_buf[USD_TEST_SIZE];

/** @brief Буфер прочитанных данных (верификация). */
static uint8_t g_s_read_buf[USD_TEST_SIZE];

/* ── Вспомогательные функции ────────────────────────────────────────────── */

static test_result_t make_fail(const char *p_detail)
{
    test_result_t result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U };
    (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", p_detail);
    return result;
}

/**
 * @brief Освободить ресурсы: закрыть файл, удалить тестовый файл,
 *        отмонтировать, deинициализировать SD host.
 *
 * Best-effort: коды ошибок FatFS игнорируются.
 * Безопасно вызывать в любом сочетании флагов.
 *
 * @param file_open  true → дескриптор g_s_file открыт и требует f_close.
 * @param mounted    true → FatFS смонтирован, выполнить f_unlink + f_unmount.
 */
static void usd_cleanup(bool file_open, bool mounted)
{
    if (file_open)
    {
        (void) f_close(&g_s_file);
    }

    if (mounted)
    {
        (void) f_unlink(USD_TEST_FILE);
        (void) f_unmount(USD_MOUNT_POINT);
    }

    (void) bsp_sd_deinit();
}

/* ── Шаг 1: Card detect ─────────────────────────────────────────────────── */

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

/* ── Шаг 2: SD host init ────────────────────────────────────────────────── */

static bool step_sd_init(test_result_t *p_out)
{
    bsp_usb_cdc_poll();

    if (bsp_sd_init() != BSP_OK)
    {
        *p_out = make_fail("sd init failed");
        return false;
    }

    cli_send("{\"type\":\"progress\",\"test\":\"usd\","
             "\"step\":\"init\",\"status\":\"ok\"}\n");
    return true;
}

/* ── Шаг 3: Mount ───────────────────────────────────────────────────────── */

static bool step_mount(test_result_t *p_out)
{
    bsp_usb_cdc_poll();

    FRESULT result = f_mount(&g_s_fs, USD_MOUNT_POINT, 1);

    if (result != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "mount failed: %d", (int) result);
        *p_out = make_fail(detail);
        return false;
    }

    cli_send("{\"type\":\"progress\",\"test\":\"usd\","
             "\"step\":\"mount\",\"status\":\"ok\"}\n");
    return true;
}

/* ── Шаг 4: Write ───────────────────────────────────────────────────────── */

/**
 * @brief Заполнить паттерн, создать файл FWTEST.TMP, записать USD_TEST_SIZE байт.
 *
 * При ошибке записи файл остаётся открытым — вызывающая сторона обязана
 * вызвать usd_cleanup(true, true). Состояние сообщается через p_file_left_open.
 *
 * При успехе файл закрыт до возврата.
 *
 * @param[out] p_file_left_open  true если g_s_file открыт при возврате false.
 * @param[out] p_out             Описание ошибки при возврате false.
 * @return true при успехе.
 */
static bool step_write(bool *p_file_left_open, test_result_t *p_out)
{
    *p_file_left_open = false;

    for (uint32_t i = 0U; i < USD_TEST_SIZE; i++)
    {
        g_s_write_buf[i] = (uint8_t) (i & 0xFFU);
    }

    bsp_usb_cdc_poll();

    FRESULT result = f_open(&g_s_file, USD_TEST_FILE, FA_WRITE | FA_CREATE_ALWAYS);

    if (result != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "open failed: %d", (int) result);
        *p_out = make_fail(detail);
        return false;
    }

    *p_file_left_open = true;

    UINT bytes_written = 0U;

    result = f_write(&g_s_file, g_s_write_buf, USD_TEST_SIZE, &bytes_written);

    bsp_usb_cdc_poll();

    if (result != FR_OK || bytes_written != USD_TEST_SIZE)
    {
        char detail[TEST_DETAIL_SIZE];
        if (result != FR_OK)
        {
            (void) snprintf(detail, sizeof(detail), "write failed: %d", (int) result);
        }
        else
        {
            (void) snprintf(detail, sizeof(detail), "write incomplete: %u/%u",
                            (unsigned int) bytes_written, (unsigned int) USD_TEST_SIZE);
        }
        *p_out = make_fail(detail);
        return false;
    }

    result            = f_close(&g_s_file);
    *p_file_left_open = false;
    bsp_usb_cdc_poll();

    if (result != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "close after write failed: %d", (int) result);
        *p_out = make_fail(detail);
        return false;
    }

    cli_send("{\"type\":\"progress\",\"test\":\"usd\","
             "\"step\":\"write\",\"status\":\"ok\"}\n");
    return true;
}

/* ── Шаг 5: Read + Compare ──────────────────────────────────────────────── */

/**
 * @brief Открыть FWTEST.TMP на чтение, прочитать USD_TEST_SIZE байт,
 *        сравнить с g_s_write_buf побайтово.
 *
 * При ошибке до f_close файл может оставаться открытым — состояние
 * передаётся через p_file_left_open. При успехе файл закрыт до возврата.
 *
 * @param[out] p_file_left_open  true если g_s_file открыт при возврате false.
 * @param[out] p_out             Описание ошибки при возврате false.
 * @return true при успехе.
 */
static bool step_read_compare(bool *p_file_left_open, test_result_t *p_out)
{
    *p_file_left_open = false;

    bsp_usb_cdc_poll();

    FRESULT result = f_open(&g_s_file, USD_TEST_FILE, FA_READ);

    if (result != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "open for read failed: %d", (int) result);
        *p_out = make_fail(detail);
        return false;
    }

    *p_file_left_open = true;

    (void) memset(g_s_read_buf, 0, USD_TEST_SIZE);

    UINT bytes_read = 0U;
    result          = f_read(&g_s_file, g_s_read_buf, USD_TEST_SIZE, &bytes_read);
    bsp_usb_cdc_poll();

    if (result != FR_OK || bytes_read != USD_TEST_SIZE)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "read failed: %d", (int) result);
        *p_out = make_fail(detail);
        return false;
    }

    result            = f_close(&g_s_file);
    *p_file_left_open = false;
    bsp_usb_cdc_poll();

    if (result != FR_OK)
    {
        char detail[TEST_DETAIL_SIZE];
        (void) snprintf(detail, sizeof(detail), "close after read failed: %d", (int) result);
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

static test_result_t usd_run(void)
{
    test_result_t fail_result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U, .detail = { 0 } };
    bool file_open            = false;

    /* Шаг 1: проверка карты (bsp_sd_init ещё не вызван → без cleanup) */
    if (!step_card_detect(&fail_result))
    {
        return fail_result;
    }

    /* Шаг 2: init (mount ещё не вызван → только deinit при провале) */
    if (!step_sd_init(&fail_result))
    {
        (void) bsp_sd_deinit();
        return fail_result;
    }

    /* Шаг 3: mount */
    if (!step_mount(&fail_result))
    {
        (void) bsp_sd_deinit();
        return fail_result;
    }

    /* Шаги 4-5: write + read/compare (файловая система смонтирована) */
    if (!step_write(&file_open, &fail_result))
    {
        usd_cleanup(file_open, true);
        return fail_result;
    }

    if (!step_read_compare(&file_open, &fail_result))
    {
        usd_cleanup(file_open, true);
        return fail_result;
    }

    /* Успех: файл закрыт внутри step_read_compare, раздел ещё смонтирован */
    usd_cleanup(false, true);

    return (test_result_t){
        .status      = TEST_STATUS_PASS,
        .duration_ms = 0U,
        .detail      = { 0 },
    };
}

/* ── Дескриптор модуля ──────────────────────────────────────────────────── */

const test_module_t K_TEST_USD = {
    .id                 = "usd",
    .name               = "microSD (SDIO)",
    .critical           = false,
    .requires_hil       = false,
    .pre_confirm_prompt = "Вставьте карту microSD и нажмите OK",
    .init               = NULL,
    .run                = usd_run,
    .deinit             = NULL,
};