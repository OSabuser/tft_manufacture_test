/**
 * @file  sd_update.c
 * @brief Реализация — см. sd_update.h.
 *
 * Гейт принятия кандидата — двухступенчатый:
 *   1. Лёгкий пик заголовка (magic + версия) сразу с SD, до касания flash —
 *      достаточно для решения install/skip (update_policy_decide()).
 *   2. Полная криптографическая проверка (hash+ECDSA) — уже ПОСЛЕ записи в
 *      целевой слот, переиспользованием slot_version_get() (та же функция,
 *      что и для пика активного слота). Если кандидат подписан неверно —
 *      slot_version_get() на целевом слоте вернёт false, а последующий
 *      (единственный) boot_go() в main.c просто не выберет этот слот и
 *      останется на прежнем валидном — отдельный shim "flash_area поверх SD-
 *      файла" не нужен.
 *
 * Целевой слот при установке — всегда НЕ активный (см. update_policy.h):
 * уже выбранный на этот момент слот этой функцией никогда не стирается.
 * Исключение — recovery-режим (Фаза 6b, update_policy_decide(recovery_mode)):
 * там целевой слот всегда Slot A, независимо от того, что было активно.
 */

#include "sd_update.h"

#include "bootutil/image.h"
#include "bsp/sd.h"
#include "bsp/usb_cdc.h"
#include "bsp/wdog.h"
#include "ff.h"
#include "flash_map.h"
#include "protocol.h"
#include "slot_version.h"
#include "update_policy.h"

#include <string.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

#define SD_UPDATE_MOUNT_POINT "2:/"
#define SD_UPDATE_FILE_PATH   "2:/TFT_APP.BIN"

/** @brief Размер чанка потокового копирования SD → flash. */
#define SD_UPDATE_CHUNK_SIZE 4096U

/* ── Состояние модуля ────────────────────────────────────────────────────── */

static FATFS g_s_fs;
static FIL g_s_file;
static uint8_t g_s_chunk_buf[SD_UPDATE_CHUNK_SIZE];
static uint8_t g_s_verify_buf[SD_UPDATE_CHUNK_SIZE];

/* ── Вспомогательные функции ───────────────────────────────────────────── */

static update_policy_slot_state_t peek_slot(uint8_t fa_id)
{
    update_policy_slot_state_t state;
    state.valid = slot_version_get(fa_id, &state.version);
    return state;
}

/**
 * @brief Прочитать заголовок кандидата с начала уже открытого файла.
 * @retval true   magic верный — версия в *p_out_ver, позиция файла = sizeof(header).
 * @retval false  Ошибка чтения либо неверный magic.
 */
static bool read_candidate_header(struct image_version *p_out_ver)
{
    struct image_header hdr;
    UINT br = 0U;

    FRESULT fr = f_read(&g_s_file, &hdr, sizeof(hdr), &br);
    if ((fr != FR_OK) || (br != sizeof(hdr)) || (hdr.ih_magic != IMAGE_MAGIC))
    {
        return false;
    }

    *p_out_ver = hdr.ih_ver;
    return true;
}

/**
 * @brief Стереть целевой слот и потоково скопировать в него файл-кандидат
 *        (с начала файла — заголовок читается заново), сверяя каждый
 *        записанный чанк немедленным обратным чтением.
 *
 * @return true при успехе (весь файл скопирован и каждый чанк совпал).
 */
static bool erase_and_copy_candidate(const struct flash_area *p_fap, uint32_t file_size)
{
    if (file_size > p_fap->fa_size)
    {
        return false;
    }

    if (flash_area_erase(p_fap, 0U, p_fap->fa_size) != 0)
    {
        return false;
    }

    if (f_lseek(&g_s_file, 0) != FR_OK)
    {
        return false;
    }

    uint32_t offset = 0U;
    while (offset < file_size)
    {
        bsp_usb_cdc_poll();
        bsp_wdog_refresh(); /* потоковое копирование — реальный прогресс на чанк */

        uint32_t want = file_size - offset;
        if (want > SD_UPDATE_CHUNK_SIZE)
        {
            want = SD_UPDATE_CHUNK_SIZE;
        }

        UINT br = 0U;
        if ((f_read(&g_s_file, g_s_chunk_buf, want, &br) != FR_OK) || (br != want))
        {
            return false;
        }

        if (flash_area_write(p_fap, offset, g_s_chunk_buf, want) != 0)
        {
            return false;
        }

        if ((flash_area_read(p_fap, offset, g_s_verify_buf, want) != 0) ||
            (memcmp(g_s_chunk_buf, g_s_verify_buf, want) != 0))
        {
            return false;
        }

        offset += want;
    }

    bsp_usb_cdc_poll();
    return true;
}

/* ── Основной сценарий ────────────────────────────────────────────────── */

/**
 * @return true, если target_slot после этого вызова содержит новый,
 *         подтверждённый (slot_version_get()) образ — см. sd_update.h.
 */
static bool run_update(bool button_held, bool recovery_mode)
{
    struct image_version candidate_ver;
    struct image_version installed_ver;
    update_policy_slot_state_t slot_a;
    update_policy_slot_state_t slot_b;
    update_policy_result_t decision;
    const struct flash_area *p_fap = NULL;
    uint32_t file_size;
    bool copy_ok;
    bool result = false;

    if (bsp_sd_init() != BSP_OK)
    {
        return false;
    }

    if (f_mount(&g_s_fs, SD_UPDATE_MOUNT_POINT, 1) != FR_OK)
    {
        (void) bsp_sd_deinit();
        return false; /* нет карты/файловой системы — штатно, не ошибка */
    }

    if (f_open(&g_s_file, SD_UPDATE_FILE_PATH, FA_READ) != FR_OK)
    {
        (void) f_unmount(SD_UPDATE_MOUNT_POINT);
        (void) bsp_sd_deinit();
        return false; /* TFT_APP.BIN отсутствует — тоже штатно */
    }

    if (!read_candidate_header(&candidate_ver))
    {
        protocol_send_error("SD_CANDIDATE_INVALID");
        goto cleanup;
    }

    file_size = (uint32_t) f_size(&g_s_file);
    slot_a    = peek_slot(0U);
    slot_b    = peek_slot(1U);
    /* button_held — сэмплирован при старте в main.c и передан сюда (см.
     * sd_update.h). recovery_mode — ослабленный gate Фазы 6b, см.
     * update_policy.h; решение "входить ли в recovery" — не здесь. */
    decision = update_policy_decide(&slot_a, &slot_b, &candidate_ver, button_held, recovery_mode);

    if (decision.action == UPDATE_POLICY_SKIP)
    {
        protocol_send_status("update_skipped");
        goto cleanup;
    }

    protocol_send_status("installing");

    if (flash_area_open((uint8_t) decision.target_slot, &p_fap) != 0)
    {
        protocol_send_error("SD_INSTALL_WRITE_FAILED");
        goto cleanup;
    }

    copy_ok = erase_and_copy_candidate(p_fap, file_size);
    flash_area_close(p_fap);

    if (!copy_ok)
    {
        protocol_send_error("SD_INSTALL_WRITE_FAILED");
        goto cleanup;
    }

    if (!slot_version_get((uint8_t) decision.target_slot, &installed_ver))
    {
        protocol_send_error("SD_INSTALL_REJECTED");
        goto cleanup;
    }

    /* target_slot подтверждён валидным — установка состоялась независимо от
     * исхода стирания "второго" слота ниже (оно диагностируется отдельным
     * protocol_send_error, но не отменяет уже подтверждённый результат). */
    result = true;

    /* Форс. даунгрейд и recovery (см. update_policy.h): без стирания
     * прежнего активного/Slot Б он остался бы валиден (и новее в обычном
     * режиме) и снова выиграл бы в boot_go() — даунгрейд/recovery физически
     * записались бы, но не загрузились. Стираем ТОЛЬКО теперь, когда новый
     * образ уже подтверждён валидным — на диске никогда не бывает нуля
     * рабочих слотов. */
    if (decision.erase_previous_active)
    {
        const struct flash_area *p_peer_fap;
        uint8_t peer_slot = (decision.target_slot == UPDATE_POLICY_SLOT_A) ? UPDATE_POLICY_SLOT_B
                                                                           : UPDATE_POLICY_SLOT_A;

        if (flash_area_open(peer_slot, &p_peer_fap) != 0)
        {
            protocol_send_error("SD_DOWNGRADE_ERASE_FAILED");
        }
        else
        {
            if (flash_area_erase(p_peer_fap, 0U, p_peer_fap->fa_size) != 0)
            {
                protocol_send_error("SD_DOWNGRADE_ERASE_FAILED");
            }
            flash_area_close(p_peer_fap);
        }
    }

cleanup:
    (void) f_close(&g_s_file);
    (void) f_unmount(SD_UPDATE_MOUNT_POINT);
    (void) bsp_sd_deinit();
    return result;
}

/* ── Public API ────────────────────────────────────────────────────────── */

bool sd_update_check(bool downgrade_button_held, bool recovery_mode)
{
    if (!bsp_sd_is_inserted())
    {
        return false;
    }

    return run_update(downgrade_button_held, recovery_mode);
}
