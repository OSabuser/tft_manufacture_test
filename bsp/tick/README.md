# bsp_tick — интеграция в firmware-проекты

`SysTick_Handler` определён внутри `tick.c` и принадлежит модулю `bsp_tick`.
Для добавления внешней логики в обработчик используй слабый хук (см. ниже).

---

## firmware/test и firmware/bootloader (bare-metal)

**CMakeLists.txt**:

```cmake
target_link_libraries(firmware_test PRIVATE bsp_tick)
```

**Порядок инициализации в main()**:

```c
board_hw_init();        // тактирование и пины (BOARD_BootClockRUN внутри)
bsp_tick_init();        // SysTick — после того как SystemCoreClock актуален
bsp_uart_init(115200);  // и далее всё что зависит от времени
```

---

## firmware/tft_app (FreeRTOS)

**FreeRTOSConfig.h** — убедиться:

```c
#define configTICK_RATE_HZ    1000   // 1 тик = 1 мс
#define configUSE_TICK_HOOK   1      // включить vApplicationTickHook
```

**board.c** — добавить hook (см. раздел про хуки ниже):

```c
#include "bsp/tick.h"

void vApplicationTickHook(void)
{
    bsp_tick_inc();  // no-op в FreeRTOS-режиме, но оставляем для единообразия
}
```

**CMakeLists.txt**:

```cmake
target_link_libraries(tft_app PRIVATE bsp_tick freertos_kernel)
target_compile_definitions(tft_app PRIVATE BSP_TICK_FREERTOS_MODE)
```

**Инициализация**:

```c
board_hw_init();
bsp_tick_init();        // no-op, но вызываем для симметрии с bare-metal
vTaskStartScheduler();  // FreeRTOS берёт SysTick себе здесь
```

---

## Добавление логики в SysTick — weak hook

Каждый BSP-модуль владеет своим прерыванием целиком. `SysTick_Handler`
живёт в `tick.c` и принадлежит `bsp_tick`. Если другому модулю нужно
выполнять работу каждый тик (watchdog, программный таймер и т.п.) —
используй слабый хук, не трогая `tick.c`.

**Как это работает:**

`tick.c` объявляет и вызывает `bsp_systick_hook()` с атрибутом `weak`.
Если никто не определил эту функцию — линкер подставляет пустую
заглушку, накладные расходы нулевые. Как только в любом `.c` файле
проекта появляется сильное определение — оно автоматически подхватывается.

```c
// tick.c (уже реализовано):
__attribute__((weak)) void bsp_systick_hook(void) { /* no-op по умолчанию */ }

void SysTick_Handler(void)
{
    bsp_tick_inc();
    bsp_systick_hook();
}
```

### **Пример: watchdog из board.c**

```c
// board.c
#include "bsp/tick.h"
#include "bsp/wdog.h"

// Переопределяем слабый хук — линкер возьмёт эту версию
void bsp_systick_hook(void)
{
    bsp_wdog_feed();
}
```

### **Пример: два действия в хуке**

```c
// board.c
void bsp_systick_hook(void)
{
    bsp_wdog_feed();
    bsp_some_other_periodic_task();
}
```

**Важно:** хук вызывается из ISR-контекста. Никаких блокирующих
операций, мьютексов или `bsp_delay()` внутри.

---

## Использование в BSP-модулях и приложении

```c
#include "bsp/tick.h"

// Таймаут (wraparound-safe):
uint32_t start = bsp_tick_get_ms();
while (!done) {
    if ((bsp_tick_get_ms() - start) >= TIMEOUT_MS) { break; }
}

// Блокирующая пауза (инициализация, datasheet-задержки):
bsp_delay(10);

// Периодическое действие без блокировки основного цикла:
static uint32_t s_last_ms = 0U;
if ((bsp_tick_get_ms() - s_last_ms) >= 500U) {
    s_last_ms = bsp_tick_get_ms();
    // ... действие ...
}
```
