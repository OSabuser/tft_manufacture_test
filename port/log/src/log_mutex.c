/**
 * @file  log_mutex.c
 * @brief FreeRTOS-реализация мьютекса для логгера.
 *
 * Перекрывает weak-хуки из port/log/src/log.c strong-определениями.
 * Компилируется только в tft_app (FreeRTOS), не в firmware/test и bootloader.
 *
 * Порядок вызовов в main() tft_app:
 *   1. board_hw_init()
 *   2. bsp_tick_init()
 *   3. bsp_uart_host_init(115200U)
 *   4. log_mutex_init()   ← создаём семафор ДО log_port_init
 *   5. log_port_init()
 *
 * @note Вызов LOG_* из ISR запрещён: xSemaphoreTake с portMAX_DELAY
 *       недопустим в ISR-контексте.
 */
#include "FreeRTOS.h"
#include "log/log.h"
#include "semphr.h"

static SemaphoreHandle_t s_log_mutex;

void log_mutex_init(void)
{
    s_log_mutex = xSemaphoreCreateMutex();
    configASSERT(s_log_mutex != NULL);
}

void log_mutex_lock(void)
{
    xSemaphoreTake(s_log_mutex, portMAX_DELAY);
}

void log_mutex_unlock(void)
{
    xSemaphoreGive(s_log_mutex);
}