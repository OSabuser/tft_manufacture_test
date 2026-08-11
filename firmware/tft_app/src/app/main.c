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
#include "crash_log.h"
#include "queue.h"
#include "task.h"
#include "timers.h"

#include <stdbool.h>

#define INPUT_POLL_PERIOD_MS 5U /* софт-таймер опроса ввода (bsp_button_poll; opto — Фаза 3.4) */

/* ── Разделяемое состояние задач (объявления — app_tasks.h) ──────────────── */

volatile bool g_display_ready                            = false;
volatile bool g_menu_active                              = false;
QueueHandle_t g_render_queue                             = NULL;
TaskHandle_t g_render_task_handle                        = NULL;
volatile dispatcher_indication_t g_dispatcher_indication = DISPATCHER_INDICATION_NONE;
boot_screen_info_t g_boot_screen_info                    = { 0 };

int main(void)
{
    board_hw_init(); /* BOARD_ConfigMPU + BOARD_InitPins + BOARD_BootClockRUN */
    bsp_led_init();
    (void) bsp_button_init(); /* GPIO настроен в BOARD_InitPins; сброс debounce */
    dispatcher_init(); /* opto IN1/IN2 (§3.4) — пины тоже уже в BOARD_InitPins */

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

/* ── Отказы: записать причину и сбросить (диагностика — app/crash_log.h) ────
 *
 * Раньше оба хука немо зависали с выключенными прерываниями, и плату через
 * 10 с добивал watchdog — снаружи это было НЕОТЛИЧИМО от «задача-кормилец не
 * получала CPU». Теперь причина попадает в `.noinit`-запись и печатается при
 * следующем старте; сброс — немедленный, чтобы не ждать watchdog.
 */

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void) task;
    crash_log_record_and_reset(CRASH_CAUSE_STACK_OVERFLOW, name, 0U, 0U);
}

void vApplicationMallocFailedHook(void)
{
    crash_log_record_and_reset(CRASH_CAUSE_MALLOC_FAILED, NULL, 0U, 0U);
}

/**
 * @brief HardFault — достать PC/LR из кадра исключения и записать причину.
 *
 * `naked` + asm: нужен НЕТРОНУТЫЙ SP на входе, чтобы понять, какой стек
 * (MSP/PSP) использовался, и найти в нём сохранённый кадр {r0-r3,r12,LR,PC,
 * xPSR}. Компилятор в обычной функции успел бы сдвинуть SP прологом.
 * Перекрывает `.weak`-заглушку из startup_MIMXRT1052.S (там бесконечный цикл).
 */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst   lr, #4            \n" /* бит 2 EXC_RETURN: какой стек */
                   "ite   eq                \n"
                   "mrseq r0, msp           \n"
                   "mrsne r0, psp           \n"
                   "b     hardfault_report  \n");
}

/** Вторая половина HardFault_Handler: p_frame — кадр исключения на стеке. */
void hardfault_report(const uint32_t *p_frame); /* вызывается только из asm выше */

void hardfault_report(const uint32_t *p_frame)
{
    /* Раскладка кадра (ARMv7-M): [0]=r0 [1]=r1 [2]=r2 [3]=r3 [4]=r12
     * [5]=LR [6]=PC [7]=xPSR. PC — инструкция, вызвавшая отказ. */
    crash_log_record_and_reset(CRASH_CAUSE_HARDFAULT, NULL, p_frame[6], p_frame[5]);
}
