/**
 * @file  slot_version.c
 * @brief Реализация — см. slot_version.h.
 *
 * Вызов bootutil_img_validate() зеркалит loader.c::boot_image_check()
 * (enc_state=NULL — шифрование образов не используется, image_index=0 —
 * MCUBOOT_IMAGE_NUMBER=1, seed=NULL/0 — FIH_PROFILE_LOW не использует RNG-
 * задержку, out_hash=NULL — хэш нам не нужен, только факт валидности+версия).
 *
 * FIH_CALL безопасен вне boot_go(): CFI-счётчик (FIH_ENABLE_CFI под LOW
 * профилем) сохраняется/инкрементируется в FIH_CFI_PRECALL_BLOCK и
 * проверяется/возвращается к сохранённому значению в FIH_CFI_POSTCALL_BLOCK —
 * пара самобалансирующаяся на каждый вызов, не накапливающееся состояние
 * между вызовами (см. fault_injection_hardening.h). Несколько вызовов подряд
 * (Slot A, Slot Б, кандидат) и последующий отдельный boot_go() не влияют друг
 * на друга через этот счётчик.
 */

#include "slot_version.h"

#include "bootutil/fault_injection_hardening.h"
#include "flash_map.h"

#include <stddef.h> /* NULL */

/*
 * Совпадает с BOOT_TMPBUF_SZ в sdk/middleware/mcuboot_opensource/boot/bootutil/
 * src/bootutil_priv.h — приватный заголовок bootutil (в src/, не в include/),
 * поэтому не включаем его напрямую. Тот же размер, что loader.c использует
 * для этого же вызова.
 */
#define SLOT_VERSION_TMPBUF_SIZE 256U

bool slot_version_get(uint8_t fa_id, struct image_version *p_out_ver)
{
    const struct flash_area *p_fap;
    if (flash_area_open(fa_id, &p_fap) != 0)
    {
        return false;
    }

    struct image_header hdr;
    bool read_ok = (flash_area_read(p_fap, 0U, &hdr, sizeof(hdr)) == 0);

    if (!read_ok || (hdr.ih_magic != IMAGE_MAGIC))
    {
        flash_area_close(p_fap);
        return false;
    }

    static uint8_t s_tmpbuf[SLOT_VERSION_TMPBUF_SIZE];
    fih_ret fih_rc;
    FIH_CALL(bootutil_img_validate, fih_rc, NULL, 0, &hdr, p_fap, s_tmpbuf, sizeof(s_tmpbuf), NULL,
             0, NULL);

    flash_area_close(p_fap);

    if (!FIH_EQ(fih_rc, FIH_SUCCESS))
    {
        return false;
    }

    *p_out_ver = hdr.ih_ver;
    return true;
}
