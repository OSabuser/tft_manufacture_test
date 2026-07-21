/**
 * @file  log_cdc.c
 * @brief USB CDC ACM адаптер логгера.
 *
 * Подключает utils_log к bsp_usb_cdc:
 *   - write callback → bsp_usb_cdc_write() (best-effort, см. port/log_cdc.h)
 *   - timestamp hook → bsp_tick_get_ms()
 */

#include "port/log_cdc.h"

#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "log/log.h"

/* -------------------------------------------------------------------------- */
/* Strong-реализация weak-хука timestamp                                       */
/* -------------------------------------------------------------------------- */

uint32_t log_get_timestamp_ms(void)
{
    return bsp_tick_get_ms();
}

/* -------------------------------------------------------------------------- */
/* Write callback                                                               */
/* -------------------------------------------------------------------------- */

static void cdc_write(const char *p_buf, size_t len, void *p_ctx)
{
    (void) p_ctx;
    if (!bsp_usb_cdc_is_ready())
    {
        return; /* хост не подключён — молча отбрасываем (best-effort) */
    }
    /* Не блокируемся на BSP_ERR_BUSY (предыдущая передача не завершена) —
     * это диагностический канал, потерянная строка не критична. */
    (void) bsp_usb_cdc_write((const uint8_t *) p_buf, len);
}

/* -------------------------------------------------------------------------- */
/* Публичный API                                                                */
/* -------------------------------------------------------------------------- */

void log_cdc_init(void)
{
    log_init(cdc_write, NULL);
}
