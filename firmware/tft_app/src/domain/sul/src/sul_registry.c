#include "domain/sul.h"
#include "domain/sul/demo.h"
#include "domain/sul/nku_can.h"
#include "domain/sul/uim.h"

#include <stddef.h>

/* НКУ-CAN — один параметр: адрес станции 0..15 (proto_slice[0], §8). */
static const sul_settings_entry_t K_NKU_CAN_SETTINGS_ENTRIES[] = {
    {
        .p_label      = "Адрес",
        .type         = SUL_SETTINGS_BYTE,
        .slice_offset = 0U,
        .min          = 0U,
        .max          = 15U,
        .p_options    = NULL,
    },
};

static const sul_settings_desc_t K_NKU_CAN_SETTINGS = {
    .p_entries = K_NKU_CAN_SETTINGS_ENTRIES,
    .count     = sizeof(K_NKU_CAN_SETTINGS_ENTRIES) / sizeof(K_NKU_CAN_SETTINGS_ENTRIES[0]),
};

/* Демо — один параметр: скорость прохождения скриптованного маршрута (§8).
 * Тест того, что дескрипторный механизм не завязан на "адрес"-подобную
 * форму параметра (НКУ-CAN) — здесь SELECT с метками, не BYTE. */
static const char *const K_DEMO_SPEED_LABELS[] = { "Медленно", "Норма", "Быстро" };

static const sul_settings_entry_t K_DEMO_SETTINGS_ENTRIES[] = {
    {
        .p_label      = "Скорость",
        .type         = SUL_SETTINGS_SELECT,
        .slice_offset = 0U,
        .min          = 0U,
        .max          = 2U,
        .p_options    = K_DEMO_SPEED_LABELS,
    },
};

static const sul_settings_desc_t K_DEMO_SETTINGS = {
    .p_entries = K_DEMO_SETTINGS_ENTRIES,
    .count     = sizeof(K_DEMO_SETTINGS_ENTRIES) / sizeof(K_DEMO_SETTINGS_ENTRIES[0]),
};

/* УИМ-6100 — один параметр: адрес индикатора. Множество НЕ непрерывно:
 * 1..40 — этажный индикатор, 46..50 — роли (кабина / главный этаж / доп.
 * главный / универсальный / вторичная кабина), 41..45 зарезервированы
 * протоколом. Выражено разрывом в дескрипторе (см. sul_settings_entry_t):
 * хранимое значение остаётся НАСТОЯЩИМ адресом, редактор просто перескакивает
 * 41..45 — трансляции индекс↔адрес нет ни в decode(), ни в HW-фильтрах. */
static const sul_settings_entry_t K_UIM_SETTINGS_ENTRIES[] = {
    {
        .p_label      = "Адрес",
        .type         = SUL_SETTINGS_BYTE,
        .slice_offset = 0U,
        .min          = 1U,
        .max          = 50U,
        .gap_from     = 41U,
        .gap_to       = 45U,
        .p_options    = NULL,
    },
};

static const sul_settings_desc_t K_UIM_SETTINGS = {
    .p_entries = K_UIM_SETTINGS_ENTRIES,
    .count     = sizeof(K_UIM_SETTINGS_ENTRIES) / sizeof(K_UIM_SETTINGS_ENTRIES[0]),
};

/* Ctx каждого драйвера — статика, живёт постоянно (см. domain/sul.h,
 * .p_ctx). Инициализация — sul_registry_init(). */
static nku_can_ctx_t g_s_nku_can_ctx;
static demo_ctx_t g_s_demo_ctx;
static uim_ctx_t g_s_uim_ctx;

static const sul_driver_t s_registry[] = {
    {
        .id         = SUL_PROTOCOL_NKU_CAN,
        .p_name     = "НКУ-CAN",
        .decode     = nku_can_decode,
        .p_settings = &K_NKU_CAN_SETTINGS,
        .p_ctx      = &g_s_nku_can_ctx,
        .take_pending_write = nku_can_take_pending_write, /* удалённая адресация, §3.5 */
        .connection_timeout_ms = 3000U, /* (~3с на отметку потери связи) */
    },
    {
        .id         = SUL_PROTOCOL_DEMO,
        .p_name     = "Демо",
        .decode     = demo_decode,
        .p_settings = &K_DEMO_SETTINGS,
        .p_ctx      = &g_s_demo_ctx,
        /* .take_pending_write не задан — демо никогда не пишет settings сам */
        .connection_timeout_ms =
            SUL_CONNECTION_TIMEOUT_DISABLED, /* синтетический источник, обрыва не бывает */
    },
    {
        .id         = SUL_PROTOCOL_UIM,
        .p_name     = "УИМ-6100",
        .decode     = uim_decode,
        .p_settings = &K_UIM_SETTINGS,
        .p_ctx      = &g_s_uim_ctx,
        /* .take_pending_write не задан — UIM никогда не пишет settings сам */
        .take_pending_tx = uim_take_pending_tx, /* обязательный отклик станции */
        .connection_timeout_ms = 3000U, /* (~3с на отметку потери связи) */
    },
};

#define REGISTRY_COUNT (sizeof(s_registry) / sizeof(s_registry[0]))

void sul_registry_init(void)
{
    nku_can_init(&g_s_nku_can_ctx);
    demo_init(&g_s_demo_ctx);
    uim_init(&g_s_uim_ctx);
}

/* Активный id (§8) — толкает app-слой из settings_device_t.protocol_id через
 * sul_registry_set_active(); дефолт — первая запись реестра (никогда NULL,
 * даже до первого вызова set_active(), напр. на самых ранних этапах bringup). */
static uint8_t g_s_active_id = SUL_PROTOCOL_NKU_CAN;

const sul_driver_t *sul_registry_active(void)
{
    const sul_driver_t *p_driver = sul_registry_find(g_s_active_id);
    return (p_driver != NULL) ? p_driver : &s_registry[0];
}

const sul_driver_t *sul_registry_find(uint8_t id)
{
    for (size_t i = 0; i < REGISTRY_COUNT; i++)
    {
        if (s_registry[i].id == id)
        {
            return &s_registry[i];
        }
    }
    return NULL;
}

void sul_registry_set_active(uint8_t id)
{
    if (sul_registry_find(id) != NULL)
    {
        g_s_active_id = id;
    }
    /* неизвестный id — игнорируется, s_active_id не меняется (см. sul.h) */
}

uint8_t sul_registry_count(void)
{
    return (uint8_t) REGISTRY_COUNT;
}
