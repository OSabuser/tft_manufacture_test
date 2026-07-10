/**
 * @file  slot_version.h
 * @brief Read-only "пик" версии образа в слоте bootutil — без побочных
 *        эффектов на flash.
 *
 * В отличие от boot_go(): для образа, ни разу не подтверждённого приложением,
 * bootutil (Direct-XIP-Revert) пишет copy_done в трейлер слота уже на этапе
 * выбора — вызов boot_go() второй раз за одну сессию питания принял бы этот
 * флаг за "образ уже грузился и не подтвердился" и стёр бы его (см.
 * firmware/bootloader/PLAN.md, Фаза 3). slot_version_get() читает и валидирует
 * слот через flash_area_read()/bootutil_img_validate() напрямую — оба
 * read-only (image_validate.c не пишет в flash), в обход boot_go().
 *
 * Валидация — полная (hash + ECDSA-подпись через bootutil_img_validate()),
 * тот же путь, что loader.c::boot_image_check() использует для каждого слота
 * при штатной загрузке, а не самодельная проверка одного заголовка.
 */

#ifndef SLOT_VERSION_H_
#define SLOT_VERSION_H_

#include "bootutil/image.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Прочитать и провалидировать образ в слоте fa_id.
 *
 * @param[in]  fa_id      ID области (см. sysflash.h) — 0 = Slot A, 1 = Slot Б.
 * @param[out] p_out_ver  Версия образа при успехе. Не тронут при false.
 * @retval true   Валидный образ (magic, hash и подпись прошли) — версия в *p_out_ver.
 * @retval false  Слот пуст/повреждён/подпись не прошла, либо ошибка чтения/открытия.
 */
bool slot_version_get(uint8_t fa_id, struct image_version *p_out_ver);

#endif /* SLOT_VERSION_H_ */
