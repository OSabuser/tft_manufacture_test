
/**
 * @file  main.c
 * @brief firmware_test — точка входа.
 *
 * Минимальный smoke-тест QSPI в XIP-конфигурации:
 *   1) Инициализация QSPI.
 *   2) Чтение JEDEC ID.
 *   3) Erase одного сектора в конце Flash.
 *   4) Запись одной страницы и чтение назад.
 *   5) Сравнение буферов и LED-индикация PASS/FAIL.
 */
#include "board.h"
#include "bsp/led.h"
#include "bsp/qspi_flash.h"
#include "bsp/tick.h"

#include <stdbool.h>
#include <stdint.h>

static volatile uint32_t g_qspi_test_step           = 0U;
static volatile uint32_t g_qspi_test_addr           = 0U;
static volatile uint32_t g_qspi_test_mismatch_index = 0xFFFFFFFFUL;
static volatile bsp_qspi_jedec_t g_qspi_test_jedec  = { 0U, 0U };

static bool bytes_equal(const uint8_t *p_lhs, const uint8_t *p_rhs, size_t size)
{
    for (size_t i = 0U; i < size; i++)
    {
        if (p_lhs[i] != p_rhs[i])
        {
            g_qspi_test_mismatch_index = (uint32_t) i;
            return false;
        }
    }
    return true;
}

int main(void)
{
    const uint32_t ERROR_BLINK_MS = 80U;
    const uint32_t PASS_BLINK_MS  = 350U;

    board_hw_init();
    bsp_led_init();
    bsp_tick_init();

    g_qspi_test_step = 1U;
    if (bsp_qspi_init() != BSP_OK)
    {
        goto FAIL;
    }

    g_qspi_test_step = 2U;
    if (bsp_qspi_read_jedec_id((bsp_qspi_jedec_t *) &g_qspi_test_jedec) != BSP_OK)
    {
        goto FAIL;
    }

    if (g_qspi_test_jedec.manufacturer_id != BSP_QSPI_MFR_WINBOND)
    {
        goto FAIL;
    }

    g_qspi_test_step          = 3U;
    const uint32_t flash_size = bsp_qspi_flash_size();
    if (flash_size < BSP_QSPI_SECTOR_SIZE)
    {
        goto FAIL;
    }
    g_qspi_test_addr = flash_size - BSP_QSPI_SECTOR_SIZE;

    uint8_t tx[BSP_QSPI_PAGE_SIZE];
    uint8_t rx[BSP_QSPI_PAGE_SIZE];

    for (uint32_t i = 0U; i < BSP_QSPI_PAGE_SIZE; i++)
    {
        tx[i] = (uint8_t) (0xA5U ^ i ^ g_qspi_test_jedec.manufacturer_id ^
                           (uint8_t) g_qspi_test_jedec.device_id);
        rx[i] = 0U;
    }

    g_qspi_test_step = 4U;
    if (bsp_qspi_erase_sector(g_qspi_test_addr) != BSP_OK)
    {
        goto FAIL;
    }

    g_qspi_test_step = 5U;
    if (bsp_qspi_write_page(g_qspi_test_addr, tx) != BSP_OK)
    {
        goto FAIL;
    }

    g_qspi_test_step = 6U;
    if (bsp_qspi_read(g_qspi_test_addr, rx, sizeof(rx)) != BSP_OK)
    {
        goto FAIL;
    }

    g_qspi_test_step = 7U;
    if (!bytes_equal(tx, rx, sizeof(rx)))
    {
        goto FAIL;
    }

    g_qspi_test_step = 8U;
    bsp_led_on(LED_APP);
    while (1)
    {
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(PASS_BLINK_MS);
    }

FAIL:
    g_qspi_test_step |= 0x80000000UL;
    while (1)
    {
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(ERROR_BLINK_MS);
    }
}
