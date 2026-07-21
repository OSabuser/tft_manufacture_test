#include "services/settings_store.h"

#include "bsp/qspi_flash.h"
#include "services/partition.h"
#include "settings_codec.h"

/* Активные настройки — единственный экземпляр модуля. */
static settings_t g_settings;

void settings_store_init_defaults(void)
{
    g_settings = settings_defaults();
}

bsp_status_t settings_store_load(void)
{
    settings_page_t page;

    if (bsp_qspi_read(TFT_APP_QSPI_SETTINGS_OFFSET, (uint8_t *) &page, sizeof(page)) != BSP_OK)
    {
        g_settings = settings_defaults();
        return BSP_ERR_HW;
    }

    if (!settings_deserialize(&page, &g_settings))
    {
        g_settings = settings_defaults();
        return BSP_ERR_INVALID;
    }

    return BSP_OK;
}

bsp_status_t settings_store_save(void)
{
    /* static: не 4 КБ на стеке + гарантия выравнивания под постраничную запись. */
    static settings_page_t s_page;
    settings_serialize(&g_settings, &s_page);

    if (bsp_qspi_erase_sector(TFT_APP_QSPI_SETTINGS_OFFSET) != BSP_OK)
    {
        return BSP_ERR_HW;
    }

    const uint32_t PAGES = BSP_QSPI_SECTOR_SIZE / BSP_QSPI_PAGE_SIZE;
    for (uint32_t i = 0U; i < PAGES; ++i)
    {
        const uint32_t ADDR      = TFT_APP_QSPI_SETTINGS_OFFSET + i * BSP_QSPI_PAGE_SIZE;
        const uint8_t *p_page_src = (const uint8_t *) &s_page + i * BSP_QSPI_PAGE_SIZE;
        if (bsp_qspi_write_page(ADDR, p_page_src) != BSP_OK)
        {
            return BSP_ERR_HW;
        }
    }

    return BSP_OK;
}

const settings_t *settings_store_get(void)
{
    return &g_settings;
}

settings_t *settings_store_get_mutable(void)
{
    return &g_settings;
}

void settings_store_reset_user_defaults(void)
{
    const settings_t DEFAULTS = settings_defaults();
    g_settings.user          = DEFAULTS.user;
}
