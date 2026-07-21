/*
 * FreeRTOSConfig.h — конфигурация ядра для tft_app (MIMXRT1052, Cortex-M7F).
 *
 * Порт: GCC/ARM_CM4F (используется и для cm7f, см.
 * sdk/rtos/freertos/freertos-kernel/CMakeLists.txt). Схема кучи — heap_4.
 *
 * Фаза 0: configTOTAL_HEAP_SIZE держим скромным во внутренней RAM (heap_4
 * размещает свой массив в .bss → DTCM). На фазе дисплея куча (и фреймбуферы)
 * переедут в SDRAM — тогда пересмотреть размер и размещение.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock; /* задаётся менеджером тактирования */
#endif

/* ── Планировщик ─────────────────────────────────────────────────────────── */
#define configUSE_PREEMPTION            1
#define configUSE_TICKLESS_IDLE         0
#define configCPU_CLOCK_HZ              (SystemCoreClock)
/* Без cast — bsp/tick.c (BSP_TICK_FREERTOS_MODE) сравнивает это значение в
 * `#if configTICK_RATE_HZ != 1000`; препроцессор не умеет разбирать cast
 * внутри #if ("missing binary operator"). Для C-кода бит-в-бит то же самое —
 * TickType_t получается неявным преобразованием на месте использования. */
#define configTICK_RATE_HZ             1000
#define configMAX_PRIORITIES            8
#define configMINIMAL_STACK_SIZE       ((unsigned short)128)
#define configMAX_TASK_NAME_LEN         20
#define configUSE_16_BIT_TICKS          0
#define configIDLE_SHOULD_YIELD         1
#define configUSE_TASK_NOTIFICATIONS    1
#define configUSE_MUTEXES               1
#define configUSE_RECURSIVE_MUTEXES     1
#define configUSE_COUNTING_SEMAPHORES   1
#define configQUEUE_REGISTRY_SIZE       8
#define configUSE_QUEUE_SETS            0
#define configUSE_TIME_SLICING          1
#define configUSE_NEWLIB_REENTRANT      0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configSTACK_DEPTH_TYPE          uint32_t

/* ── Память ──────────────────────────────────────────────────────────────── */
#define configFRTOS_MEMORY_SCHEME       4          /* heap_4.c */
#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE          ((size_t)(0x8000)) /* 32 КБ (Фаза 0, DTCM) */
#define configAPPLICATION_ALLOCATED_HEAP 0

/* ── Hooks (строгая диагностика на этапе разработки) ─────────────────────── */
#define configUSE_IDLE_HOOK             0
#define configUSE_TICK_HOOK             0
#define configCHECK_FOR_STACK_OVERFLOW  2   /* → vApplicationStackOverflowHook */
#define configUSE_MALLOC_FAILED_HOOK    1   /* → vApplicationMallocFailedHook  */
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

/* ── Статистика/трассировка ──────────────────────────────────────────────── */
#define configGENERATE_RUN_TIME_STATS   0
#define configUSE_TRACE_FACILITY        1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* ── Со-рутины (не используем) ───────────────────────────────────────────── */
#define configUSE_CO_ROUTINES           0
#define configMAX_CO_ROUTINE_PRIORITIES 2

/* ── Программные таймеры ─────────────────────────────────────────────────── */
#define configUSE_TIMERS                1
#define configTIMER_TASK_PRIORITY       (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH        10
#define configTIMER_TASK_STACK_DEPTH    (configMINIMAL_STACK_SIZE * 2)

/* ── Ловим ошибки на разработке ──────────────────────────────────────────── */
#define configASSERT(x)                 \
  if ((x) == 0)                         \
  {                                     \
    taskDISABLE_INTERRUPTS();           \
    for (;;)                            \
    {                                   \
    }                                   \
  }

/* ── Опциональные API ────────────────────────────────────────────────────── */
#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_xResumeFromISR          1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_vTaskDelay              1
#define INCLUDE_xTaskGetSchedulerState  1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle  0
#define INCLUDE_eTaskGetState           0
#define INCLUDE_xTimerPendFunctionCall  1
#define INCLUDE_xTaskAbortDelay         0
#define INCLUDE_xTaskGetHandle          0
#define INCLUDE_xTaskResumeFromISR      1

/* ── Прерывания Cortex-M (RT1052 NVIC = 4 бита приоритета) ───────────────── */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      ((1U << configPRIO_BITS) - 1)
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 2

#define configKERNEL_INTERRUPT_PRIORITY \
  (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ── Маппинг обработчиков порта на CMSIS-имена (vector table) ────────────── */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
