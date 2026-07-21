/**
 * @file  settings_store.h
 * @brief Персистентные настройки индикатора (ARCH.md §8, §10) — ЯДРО.
 *
 * Хранит **ядро** настроек (ярусы A «железобетонные» + B «протокольные») в
 * фиксированном секторе QSPI `0x450000` (§10) с magic/version/CRC32. Клиентский
 * UX-«зоопарк» (ярус C: лого, сдвиги/маска этажей, метки) сюда НЕ входит — он в
 * TLV рядом с layout-конфигом (§11), добавляется без правок этого ядра.
 *
 * Ярусы:
 *   A. Железобетонные (все клиенты): вес/вместимость, громкости, серийник, год.
 *   B. Протокольные (§8): активный протокол трактует `proto_slice` через
 *      свой дескриптор `sul_settings_desc_t` (НКУ-CAN: proto_slice[0] = адрес).
 *
 * Формат/сериализация (magic/version/CRC, дефолты) — чистые функции в
 * settings_codec.c, host-тестируются без QSPI. Этот модуль — тонкий flash-адаптер
 * поверх них (`bsp_qspi_flash`).
 */

#ifndef SERVICES_SETTINGS_STORE_H_
#define SERVICES_SETTINGS_STORE_H_

#include "bsp/status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ── Константы ───────────────────────────────────────────────────────────── */

#define SETTINGS_SERIAL_LEN      10U /* серийный номер, ASCII + '\0'            */
#define SETTINGS_PROTO_SLICE_LEN 8U  /* запас под протокольные параметры (§8)   */
#define SETTINGS_CORE_RESERVED   8U  /* задел под будущие ядровые поля A/B      */
#define SETTINGS_VOLUME_LEVELS   5U  /* индексы громкости: OFF/25/50/75/100     */

/* ── Устройство/провиженинг (меняется редко, не в пользовательском меню) ──── */

typedef struct
{
    uint8_t panel_type;  /**< bsp_display_type_t; хардкод TFT8, provisioning — Фаза 9 */
    uint8_t protocol_id; /**< активный протокол реестра sul (NKU_CAN = 0)             */
    uint8_t log_enabled; /**< рантайм-тумблер логов (под-шаг 3.6)                     */
    uint8_t _pad;
} settings_device_t;

/* ── Пользовательские (редактируются в меню на объекте) ──────────────────── */

typedef struct
{
    /* Ярус A — железобетонные (хранятся сейчас; редактор/эффект — Фазы 5/6). */
    uint16_t max_load_kg;      /**< грузоподъёмность, кг  0..9999            */
    uint8_t  max_cap_persons;  /**< вместимость, чел      0..99              */
    uint8_t  sound_volume_idx; /**< 0..SETTINGS_VOLUME_LEVELS-1              */
    uint8_t  music_volume_idx;
    uint8_t  year_production; /**< 0 = скрыть, иначе 2000+N (25 = 2025)      */
    char     serial[SETTINGS_SERIAL_LEN]; /**< ASCII, '\0'-терминирован     */

    /* Ярус B — протокольные: активный протокол трактует slice через
       sul_settings_desc_t (§8). НКУ-CAN: proto_slice[0] = адрес 0..15. */
    uint8_t proto_slice[SETTINGS_PROTO_SLICE_LEN];
} settings_user_t;

/* ── Ядро настроек ───────────────────────────────────────────────────────── */

typedef struct
{
    settings_device_t device;
    settings_user_t   user;
    uint8_t _reserved[SETTINGS_CORE_RESERVED]; /**< размер фикс — offset'ы полей стабильны */
} settings_t;

/* ── API ─────────────────────────────────────────────────────────────────── */

/**
 * @brief Инициализировать активные настройки значениями по умолчанию (без QSPI).
 *
 * Вызывать, когда QSPI недоступен (или до settings_store_load()) — get() после
 * этого всегда валиден. settings_store_load() при валидном флеше перезапишет.
 */
void settings_store_init_defaults(void);

/**
 * @brief Загрузить настройки из QSPI (сектор §10).
 *
 * Предусловие: bsp_qspi_init() уже вызван. Читает сектор, проверяет
 * magic/version/CRC32; при любой невалидности — загружает дефолты (настройки
 * всё равно валидны).
 *
 * @retval BSP_OK          загружено с флеша.
 * @retval BSP_ERR_INVALID magic/version/CRC не совпал — загружены дефолты.
 * @retval BSP_ERR_HW      ошибка чтения флеша — загружены дефолты.
 */
bsp_status_t settings_store_load(void);

/**
 * @brief Сохранить текущие настройки на QSPI.
 *
 * Стирает рабочий сектор, пересчитывает CRC32, пишет постранично. Вызывать по
 * подтверждению в меню (3.2). Предусловие: bsp_qspi_init().
 *
 * @retval BSP_OK / BSP_ERR_HW.
 */
bsp_status_t settings_store_save(void);

/** @brief Указатель на активные настройки (только чтение). Валиден после init/load. */
const settings_t *settings_store_get(void);

/** @brief Указатель на активные настройки для изменения (меню). save() — отдельно. */
settings_t *settings_store_get_mutable(void);

/** @brief Сбросить пользовательский ярус к дефолтам (device не трогает). save() — отдельно. */
void settings_store_reset_user_defaults(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_SETTINGS_STORE_H_ */
