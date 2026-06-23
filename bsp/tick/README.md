# bsp_tick — системный таймер

SysTick-based таймер с миллисекундным счётчиком. Используется всеми BSP-модулями
для таймаутов и периодических операций. Совместим с FreeRTOS.

---

## API

```c
void     bsp_tick_init(void);
uint32_t bsp_tick_get_ms(void);
void     bsp_delay(uint32_t ms);
void     bsp_tick_inc(void);         /* вызывать из SysTick_Handler / vApplicationTickHook */
```

`bsp_tick_get_ms()` корректно обрабатывает wraparound (`uint32_t` переполняется
через ~49 суток) — используй паттерн с вычитанием:

```c
uint32_t start = bsp_tick_get_ms();
while ((bsp_tick_get_ms() - start) < TIMEOUT_MS) { /* ждать */ }
```

---

## Быстрый старт

```c
#include "bsp/tick.h"

/* Таймаут: */
uint32_t start = bsp_tick_get_ms();
while (!done) {
    if ((bsp_tick_get_ms() - start) >= TIMEOUT_MS) { break; }
}

/* Блокирующая пауза: */
bsp_delay(10);

/* Периодическое действие без блокировки: */
static uint32_t s_last_ms = 0U;
if ((bsp_tick_get_ms() - s_last_ms) >= 500U) {
    s_last_ms = bsp_tick_get_ms();
    /* действие */
}
```

---

## Интеграция

### bare-metal (`firmware/test`, `firmware/bootloader`)

`SysTick_Handler` определён в `tick.c` и принадлежит модулю целиком.

```cmake
target_link_libraries(firmware_test PRIVATE bsp_tick)
```

```c
/* main.c — порядок инициализации: */
board_hw_init();   /* устанавливает SystemCoreClock */
bsp_tick_init();   /* после board_hw_init() */
```

### FreeRTOS (`firmware/tft_app`)

В FreeRTOS-режиме SysTick захватывается планировщиком. `bsp_tick_init()` —
no-op, `bsp_tick_inc()` вызывается из `vApplicationTickHook`.

```c
/* FreeRTOSConfig.h */
#define configTICK_RATE_HZ  1000
#define configUSE_TICK_HOOK 1
```

```c
/* board.c */
#include "bsp/tick.h"

void vApplicationTickHook(void)
{
    bsp_tick_inc();
}
```

```cmake
target_link_libraries(tft_app PRIVATE bsp_tick freertos_kernel)
target_compile_definitions(tft_app PRIVATE BSP_TICK_FREERTOS_MODE)
```

### Добавление логики в SysTick — weak hook

`tick.c` объявляет `bsp_systick_hook()` с атрибутом `weak`. Определи
эту функцию в любом `.c` файле проекта — линкер подхватит автоматически.
Никаких изменений в `tick.c` не требуется.

```c
/* tick.c (уже реализовано): */
__attribute__((weak)) void bsp_systick_hook(void) {}

void SysTick_Handler(void)
{
    bsp_tick_inc();
    bsp_systick_hook();
}
```

```c
/* board.c — пример: watchdog из хука */
void bsp_systick_hook(void)
{
    bsp_wdog_feed();
}
```

**Важно:** хук вызывается из ISR-контекста. Блокирующие операции, мьютексы
и `bsp_delay()` внутри запрещены.

---

## CMake

```cmake
target_link_libraries(firmware_test PRIVATE bsp_tick)
```

**Зависимости модуля:**

| Зависимость | Тип    | Описание                       |
| ----------- | ------ | ------------------------------ |
| `bsp_board` | PUBLIC | Транзитивно: `SystemCoreClock` |
