
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "fsl_common.h"
#include "log/log.h"
#include "port/log_uart.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Проверка non-cacheable региона
 * --------------------------------------------------------------------------
 * Буфер размещён в секции NonCacheable — OCRAM @ 0x20200000.
 * MPU Region 9 настраивает эту область как Normal non-cacheable,
 * поэтому записи CPU сразу видны DMA без SCB_CleanDCache().
 * -------------------------------------------------------------------------- */

#define NCACHE_TEST_BUF_SIZE 64U
#define NCACHE_REGION_BASE   0x20200000U
#define NCACHE_REGION_END    0x20202000U /* 8 KB — размер из линкер-скрипта */

AT_NONCACHEABLE_SECTION_ALIGN(static uint8_t s_ncache_buf[NCACHE_TEST_BUF_SIZE], 4U);

static bool ncache_test_run(void)
{
    uint32_t buf_addr = (uint32_t) s_ncache_buf;

    /* Проверяем что буфер физически лежит в ожидаемом ncache регионе */
    if (buf_addr < NCACHE_REGION_BASE || buf_addr >= NCACHE_REGION_END)
    {
        LOG_E("NCACHE", "buf addr 0x%08lx outside ncache region [0x%08x..0x%08x)",
              (unsigned long) buf_addr, NCACHE_REGION_BASE, NCACHE_REGION_END);
        return false;
    }

    /* Записываем паттерн */
    for (uint32_t i = 0U; i < NCACHE_TEST_BUF_SIZE; i++)
    {
        s_ncache_buf[i] = (uint8_t) (i ^ 0xA5U);
    }

    /* Для non-cacheable памяти flush не нужен — данные уже когерентны.
     * Вызываем CleanDCache чтобы убедиться что он не ломает данные. */
    SCB_CleanDCache();

    /* Читаем обратно и сравниваем */
    for (uint32_t i = 0U; i < NCACHE_TEST_BUF_SIZE; i++)
    {
        uint8_t expected = (uint8_t) (i ^ 0xA5U);

        if (s_ncache_buf[i] != expected)
        {
            LOG_E("NCACHE", "mismatch at [%lu]: got 0x%02x expected 0x%02x", (unsigned long) i,
                  s_ncache_buf[i], expected);
            return false;
        }
    }

    return true;
}

int main(void)
{
    const uint16_t DELAY_MS      = 100;
    const uint32_t UART_BAUDRATE = 115200;
    board_hw_init();

    bsp_led_init();
    bsp_tick_init();
    bsp_uart_host_init(UART_BAUDRATE);
    log_uart_init();

    LOG_I("BOOT", "firmware_test started, tick=%lu", (unsigned long) bsp_tick_get_ms());

    if (ncache_test_run())
    {
        LOG_I("NCACHE", "OK addr=0x%08lx size=%u", (unsigned long) (uint32_t) s_ncache_buf,
              NCACHE_TEST_BUF_SIZE);
    }
    else
    {
        LOG_E("NCACHE", "FAIL");
    }

    bsp_led_on(LED_APP);

    while (1)
    {
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(DELAY_MS);
    }
}
