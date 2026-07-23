#include "services/settings_store.h"

#include "FreeRTOS.h"
#include "bsp/qspi_flash.h"
#include "semphr.h"
#include "services/partition.h"
#include "settings_codec.h"

/* Активные настройки — единственный экземпляр модуля. */
static settings_t g_settings;

/**
 * @brief Защита save() от параллельного вызова из двух задач — сейчас
 *        menu_task (сохранение при выходе из меню) и sul_rx_task (запись
 *        удалённо заданного адреса НКУ-CAN, §3.5).
 *
 * menu_task переключает `g_menu_active=false` ДО своего save() (см.
 * task_menu.c — иначе sul_rx_task лишние мс держал бы мягкую паузу) — то
 * есть sul_rx_task может увидеть «меню уже закрыто» и попытаться сохранить
 * СВОЙ адрес, пока save() из menu_task ещё физически пишет QSPI. Без
 * мьютекса — два параллельных erase+write в один сектор, порча настроек.
 *
 * Создаётся в init_defaults()/load() — оба вызываются РОВНО один раз, из
 * bringup_task, ДО xTaskCreate() menu_task/sul_rx_task (см. task_bringup.c)
 * — на момент создания гонки нет по конструкции; лениво в save() создавать
 * было бы уже НЕ безопасно (save() зовут два разных таска на протяжении
 * всей жизни системы, а не один раз при старте).
 */
static SemaphoreHandle_t g_save_mutex;

static void ensure_save_mutex(void)
{
    if (g_save_mutex == NULL)
    {
        g_save_mutex = xSemaphoreCreateMutex();
    }
}

void settings_store_init_defaults(void)
{
    ensure_save_mutex();
    g_settings = settings_defaults();
}

bsp_status_t settings_store_load(void)
{
    ensure_save_mutex();

    /* static: страница = сектор (4 КБ) — на стеке задачи (~3 КБ) переполнит его.
     * Однократный вызов при старте, не реентерабельно (как s_page в save()). */
    static settings_page_t s_page;

    if (bsp_qspi_read(TFT_APP_QSPI_SETTINGS_OFFSET, (uint8_t *) &s_page, sizeof(s_page)) != BSP_OK)
    {
        g_settings = settings_defaults();
        return BSP_ERR_HW;
    }

    if (!settings_deserialize(&s_page, &g_settings))
    {
        g_settings = settings_defaults();
        return BSP_ERR_INVALID;
    }

    return BSP_OK;
}

bsp_status_t settings_store_save(void)
{
    /* g_save_mutex гарантированно создан к этому моменту (init_defaults()/
     * load() — единственные вызывающиеся раньше, см. докстрок выше). */
    (void) xSemaphoreTake(g_save_mutex, portMAX_DELAY);

    /* static: не 4 КБ на стеке + гарантия выравнивания под постраничную запись. */
    static settings_page_t s_page;
    settings_serialize(&g_settings, &s_page);

    bsp_status_t rc = BSP_OK;

    if (bsp_qspi_erase_sector(TFT_APP_QSPI_SETTINGS_OFFSET) != BSP_OK)
    {
        rc = BSP_ERR_HW;
    }

    if (rc == BSP_OK)
    {
        const uint32_t PAGES = BSP_QSPI_SECTOR_SIZE / BSP_QSPI_PAGE_SIZE;
        for (uint32_t i = 0U; i < PAGES; ++i)
        {
            const uint32_t ADDR       = TFT_APP_QSPI_SETTINGS_OFFSET + i * BSP_QSPI_PAGE_SIZE;
            const uint8_t *p_page_src = (const uint8_t *) &s_page + i * BSP_QSPI_PAGE_SIZE;
            if (bsp_qspi_write_page(ADDR, p_page_src) != BSP_OK)
            {
                rc = BSP_ERR_HW;
                break;
            }
        }
    }

    (void) xSemaphoreGive(g_save_mutex);
    return rc;
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
