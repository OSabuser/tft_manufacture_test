/**
 * @file  uart_host.c
 * @brief Реализация bsp_uart_host (ARM target only).
 *
 * Скомпилируется только при сборке под ARM (не при BUILD_TESTS_HOST).
 * Для host unit-тестов используется uart_host_mock.c.
 */

#include "bsp/uart_host.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* SDK */
#include "fsl_clock.h"
#include "fsl_lpuart.h"

/* BSP */
#include "bsp/tick.h"

/* Utils */
#include "ring_buffer/ring_buffer.h"

#if defined(BSP_TICK_FREERTOS_MODE)
#include "FreeRTOS.h"
#include "task.h"
#endif

/* -------------------------------------------------------------------------- */
/* Конфигурация                                                                */
/* -------------------------------------------------------------------------- */

#ifndef BSP_UART_HOST_RX_BUFFER_SIZE
#define BSP_UART_HOST_RX_BUFFER_SIZE (256U)
#endif

/* Частота источника тактирования LPUART1.
 * BOARD_BootClockRUN() настраивает OSC → 24 MHz на LPUART.
 * Скорректируй если у вас другой clock source. */
#ifndef BSP_UART_HOST_SRC_CLOCK_HZ
#define BSP_UART_HOST_SRC_CLOCK_HZ (24000000U)
#endif

/* Приоритет прерывания LPUART1 (0 = наивысший на CM7). */
#ifndef BSP_UART_HOST_IRQ_PRIORITY
#define BSP_UART_HOST_IRQ_PRIORITY (5U)
#endif

/* -------------------------------------------------------------------------- */
/* Статическое состояние модуля                                                */
/* -------------------------------------------------------------------------- */

static uint8_t g_s_rx_buf[BSP_UART_HOST_RX_BUFFER_SIZE];
static ring_buffer_desc_t g_s_rx_ring;
static bool g_s_initialized = false;

/* -------------------------------------------------------------------------- */
/* ISR — владеет прерыванием LPUART1                                           */
/* -------------------------------------------------------------------------- */

void LPUART1_IRQHandler(void)
{
    /* Читаем все байты, которые накопились в RX FIFO. */
    while (LPUART_GetStatusFlags(LPUART1) & kLPUART_RxDataRegFullFlag)
    {
        uint8_t byte = LPUART_ReadByte(LPUART1);
        /* Переполнение кольцевого буфера: байт молча теряется.
         * Caller должен читать достаточно быстро. */
        (void) ring_buffer_put(&g_s_rx_ring, byte);
    }

    /* Сброс флага прерывания выполняется автоматически при чтении регистра. */
    SDK_ISR_EXIT_BARRIER;
}

/* -------------------------------------------------------------------------- */
/* Инициализация / деинициализация                                             */
/* -------------------------------------------------------------------------- */

bsp_status_t bsp_uart_host_init(uint32_t baud_rate)
{
    if (g_s_initialized)
    {
        return BSP_ERR_INIT;
    }

    /* Инициализация кольцевого буфера. */
    if (!ring_buffer_init(&g_s_rx_ring, g_s_rx_buf, BSP_UART_HOST_RX_BUFFER_SIZE))
    {
        /* Размер не степень двойки — ошибка конфигурации. */
        return BSP_ERR_INIT;
    }

    /* Тактирование LPUART1. */
    CLOCK_EnableClock(kCLOCK_Lpuart1);

    /* Настройка периферии. */
    lpuart_config_t config;
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = baud_rate;
    config.enableRx     = true;
    config.enableTx     = true;

    status_t sdk_status = LPUART_Init(LPUART1, &config, BSP_UART_HOST_SRC_CLOCK_HZ);
    if (sdk_status != kStatus_Success)
    {
        CLOCK_DisableClock(kCLOCK_Lpuart1);
        return BSP_ERR_INIT;
    }

    /* Включаем прерывание на приход байта. */
    LPUART_EnableInterrupts(LPUART1, kLPUART_RxDataRegFullInterruptEnable);
    NVIC_SetPriority(LPUART1_IRQn, BSP_UART_HOST_IRQ_PRIORITY);
    EnableIRQ(LPUART1_IRQn);

    g_s_initialized = true;
    return BSP_OK;
}

void bsp_uart_host_deinit(void)
{
    if (!g_s_initialized)
    {
        return;
    }

    DisableIRQ(LPUART1_IRQn);
    LPUART_DisableInterrupts(LPUART1, kLPUART_RxDataRegFullInterruptEnable);
    LPUART_Deinit(LPUART1);
    CLOCK_DisableClock(kCLOCK_Lpuart1);
    ring_buffer_reset(&g_s_rx_ring);

    g_s_initialized = false;
}

/* -------------------------------------------------------------------------- */
/* TX                                                                          */
/* -------------------------------------------------------------------------- */

bsp_status_t bsp_uart_host_write(const uint8_t *p_data, size_t len)
{
    if (!g_s_initialized || p_data == NULL || len == 0U)
    {
        return BSP_ERR_INIT;
    }

    LPUART_WriteBlocking(LPUART1, p_data, len);
    return BSP_OK;
}

bsp_status_t bsp_uart_host_write_str(const char *p_str)
{
    if (!g_s_initialized || p_str == NULL)
    {
        return BSP_ERR_INIT;
    }

    size_t len = strlen(p_str);
    if (len == 0U)
    {
        return BSP_OK;
    }

    LPUART_WriteBlocking(LPUART1, (const uint8_t *) p_str, len);
    return BSP_OK;
}

/* -------------------------------------------------------------------------- */
/* RX                                                                          */
/* -------------------------------------------------------------------------- */

size_t bsp_uart_host_read(uint8_t *p_buf, size_t len, uint32_t timeout_ms)
{
    if (!g_s_initialized || p_buf == NULL || len == 0U)
    {
        return 0U;
    }

    size_t received = 0U;
    uint32_t start  = bsp_tick_get_ms();

    while (received < len)
    {
        uint8_t byte;
        if (ring_buffer_get(&g_s_rx_ring, &byte))
        {
            p_buf[received++] = byte;
            /* Сбрасываем таймер после каждого принятого байта:
             * timeout_ms — межбайтовый таймаут, а не общий. */
            start = bsp_tick_get_ms();
            continue;
        }

        /* Буфер пуст — проверяем таймаут. */
        if (timeout_ms == 0U)
        {
            break;
        }

        if (timeout_ms != BSP_UART_HOST_WAIT_FOREVER)
        {
            if ((bsp_tick_get_ms() - start) >= timeout_ms)
            {
                break;
            }
        }
#if defined(BSP_TICK_FREERTOS_MODE)
        vTaskDelay(1); // ← отдаём управление планировщику на 1 тик
#endif
    }

    return received;
}

int32_t bsp_uart_host_read_byte(uint32_t timeout_ms)
{
    if (!g_s_initialized)
    {
        return -1;
    }

    uint32_t start = bsp_tick_get_ms();

    while (true)
    {
        uint8_t byte;
        if (ring_buffer_get(&g_s_rx_ring, &byte))
        {
            return (int32_t) byte;
        }

        if (timeout_ms == 0U)
        {
            break;
        }

        if (timeout_ms != BSP_UART_HOST_WAIT_FOREVER)
        {
            if ((bsp_tick_get_ms() - start) >= timeout_ms)
            {
                break;
            }
        }
    }

    return -1;
}

size_t bsp_uart_host_rx_available(void)
{
    if (!g_s_initialized)
    {
        return 0U;
    }
    return ring_buffer_count(&g_s_rx_ring);
}

void bsp_uart_host_rx_flush(void)
{
    if (!g_s_initialized)
    {
        return;
    }
    ring_buffer_reset(&g_s_rx_ring);
}