/**
 * @file  main.c
 * @brief tft_app — точка входа: board init, объекты, bringup_task, планировщик, хуки.
 *
 * ARCH §4: app-слой = задачи + wiring + main. main() создаёт только очередь,
 * софт-таймер ввода и ОДНУ задачу (bringup_task) — она сама создаёт
 * sul_rx_task/menu_task/render_task после одноразовой инициализации и
 * удаляет себя (см. app_tasks.h — полный контракт между задачами).
 *
 * Фаза 0 (каркас, подтверждено на железе): образ линкуется как валидный
 * MCUboot-слот (Direct-XIP), bootloader в него прыгает, FreeRTOS стартует,
 * WDOG кормится, образ подтверждает себя в рантайме — см. PLAN.md, Фаза 0.
 */

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "board.h"
#include "bsp/button.h"
#include "bsp/led.h"
#include "queue.h"
#include "task.h"
#include "timers.h"

#include <stdbool.h>

#define INPUT_POLL_PERIOD_MS 5U /* софт-таймер опроса ввода (bsp_button_poll; opto — Фаза 3.4) */

/* ── Разделяемое состояние задач (объявления — app_tasks.h) ──────────────── */

volatile bool g_display_ready     = false;
volatile bool g_menu_active       = false;
QueueHandle_t g_render_queue      = NULL;
TaskHandle_t g_render_task_handle = NULL;

int main(void)
{
    board_hw_init(); /* BOARD_ConfigMPU + BOARD_InitPins + BOARD_BootClockRUN */
    bsp_led_init();
    (void) bsp_button_init(); /* GPIO настроен в BOARD_InitPins; сброс debounce */

    g_render_queue = xQueueCreate(1, sizeof(render_msg_t));
    configASSERT(g_render_queue != NULL);

    /* Опрос ввода — софт-таймер (демон на высшем приоритете в системе вытесняет
     * всё остальное — нажатия не теряются). Масштабируется на opto (Фаза 3.4)
     * тем же колбэком. */
    TimerHandle_t input_timer =
        xTimerCreate("input", pdMS_TO_TICKS(INPUT_POLL_PERIOD_MS), pdTRUE, NULL, input_poll_cb);
    configASSERT(input_timer != NULL);
    (void) xTimerStart(input_timer, 0);

    /* Единственная задача, которую создаёт main — bringup_task сама создаст
     * sul_rx_task/menu_task/render_task после инициализации и удалит себя
     * (см. app_tasks.h про приоритеты/порядок). */
    (void) xTaskCreate(bringup_task, "bringup", APP_TASK_STACK_WORDS, NULL, APP_PRIORITY_BRINGUP,
                       NULL);

    vTaskStartScheduler();

    /* Сюда планировщик не возвращается. Если вернулся — не хватило кучи под
     * idle/timer задачу. WDOG сбросит плату. */
    for (;;)
    {
    }
}

/* ── FreeRTOS hooks (строгая диагностика) ────────────────────────────────── */

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void) task;
    (void) name;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
        /* WDOG сбросит плату — детерминированный отказ вместо тихой порчи. */
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
