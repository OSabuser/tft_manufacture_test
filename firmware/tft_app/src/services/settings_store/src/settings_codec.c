#include "settings_codec.h"

#include <string.h>

_Static_assert(sizeof(settings_page_t) == BSP_QSPI_SECTOR_SIZE,
               "settings_page_t must be exactly one QSPI sector");

/* ── Дефолты ─────────────────────────────────────────────────────────────── */

/* panel_type = BSP_DISPLAY_TFT8. В bsp_display_type_t это значение 2
 * (TFT4=0, TFT7=1, TFT8=2); здесь не тянем bsp/display.h в чистый модуль,
 * значение фиксируем числом (см. ARCH §9, панель — provisioning Фазы 9). */
#define SETTINGS_DEFAULT_PANEL 2U /* BSP_DISPLAY_TFT8 */

static const settings_t K_DEFAULTS = {
    .device =
        {
            .panel_type  = SETTINGS_DEFAULT_PANEL,
            .protocol_id = 0U, /* NKU_CAN */
            .log_level = 1U, /* «Инфо» — бизнес-логика из коробки; меню поменяет */
        },
    .user =
        {
            .max_load_kg      = 0U, /* «не задано» → скрыто в UI */
            .max_cap_persons  = 0U,
            .sound_volume_idx = 2U,
            .music_volume_idx = 1U,
            .year_production  = 0U,  /* скрыть */
            .serial           = "",
            .proto_slice      = {0}, /* НКУ: адрес 0 */
        },
};

settings_t settings_defaults(void)
{
    return K_DEFAULTS;
}

/* ── CRC-32 (MSB-first, poly 0x04C11DB7, init 0xFFFFFFFF, без финального xor) ─ */

uint32_t settings_crc32(const uint8_t *p_data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < len; ++i)
    {
        crc ^= (uint32_t) p_data[i] << 24U;
        for (uint8_t b = 0U; b < 8U; ++b)
        {
            crc = (crc & 0x80000000U) ? ((crc << 1U) ^ 0x04C11DB7U) : (crc << 1U);
        }
    }
    return crc;
}

/* ── Сериализация ────────────────────────────────────────────────────────── */

void settings_serialize(const settings_t *p_in, settings_page_t *p_out)
{
    memset(p_out, 0xFFU, sizeof(*p_out)); /* _pad/_reserved = 0xFF (стёртый флеш) */
    p_out->magic   = SETTINGS_MAGIC;
    p_out->version = SETTINGS_VERSION;
    p_out->data    = *p_in;
    p_out->crc32   = settings_crc32((const uint8_t *) p_out, sizeof(*p_out) - sizeof(uint32_t));
}

bool settings_deserialize(const settings_page_t *p_page, settings_t *p_out)
{
    if ((p_page->magic != SETTINGS_MAGIC) || (p_page->version != SETTINGS_VERSION))
    {
        return false;
    }

    const uint32_t CRC =
        settings_crc32((const uint8_t *) p_page, sizeof(*p_page) - sizeof(uint32_t));
    if (CRC != p_page->crc32)
    {
        return false;
    }

    *p_out = p_page->data;
    return true;
}
