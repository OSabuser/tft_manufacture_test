/**
 * @file  settings_codec.h
 * @brief Чистая сериализация/валидация ядра настроек (без QSPI) — host-тест.
 *
 * Раскладка страницы на флеше, magic/version/CRC32 и дефолты. Отделено от
 * flash-адаптера (settings_store.c), чтобы тестировать формат на хосте без
 * железа (по образцу доменных чистых модулей).
 */

#ifndef SETTINGS_CODEC_H_
#define SETTINGS_CODEC_H_

#include "bsp/qspi_flash.h" /* BSP_QSPI_SECTOR_SIZE */
#include "services/settings_store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_MAGIC   0x54465453U /* 'STFT' (little-endian) */
/* 2 — §3.9. Бамп ОБЯЗАТЕЛЕН: удаление `dummy_option` сдвинуло `proto_slice`
 * на байт вниз. Страница прежней раскладки прошла бы magic+version+CRC (CRC
 * считается по байтам страницы, они не изменились) и была бы разобрана по
 * новым полям — адрес протокола прочитался бы из бывшего `dummy_option`.
 * С бампом такие страницы отбрасываются, и применяются дефолты. */
#define SETTINGS_VERSION 2U

/**
 * @brief Страница настроек — ровно один сектор QSPI.
 *
 * CRC32 считается по всем байтам страницы КРОМЕ самого поля crc32.
 * _pad и _reserved при сохранении = 0xFF (стёртый флеш).
 */
typedef struct
{
    uint32_t   magic;
    uint8_t    version;
    uint8_t    _pad[3];
    settings_t data;
    uint8_t    _reserved[BSP_QSPI_SECTOR_SIZE - sizeof(uint32_t)  /* magic   */
                         - sizeof(uint8_t)                        /* version */
                         - 3U                                     /* _pad    */
                         - sizeof(settings_t) - sizeof(uint32_t)]; /* crc32  */
    uint32_t crc32;
} settings_page_t;

/** @brief Настройки по умолчанию (при пустом/битом флеше). */
settings_t settings_defaults(void);

/** @brief Собрать страницу из настроек: magic/version/data/reserved(0xFF)/crc32. */
void settings_serialize(const settings_t *p_in, settings_page_t *p_out);

/**
 * @brief Проверить страницу и извлечь настройки.
 * @return true — magic/version/CRC валидны, *p_out заполнен; false — иначе
 *         (*p_out не тронут; caller подставляет дефолты).
 */
bool settings_deserialize(const settings_page_t *p_page, settings_t *p_out);

/** @brief CRC-32 (poly 0x04C11DB7, MSB-first, init 0xFFFFFFFF) — как в style_updater. */
uint32_t settings_crc32(const uint8_t *p_data, size_t len);

#endif /* SETTINGS_CODEC_H_ */
