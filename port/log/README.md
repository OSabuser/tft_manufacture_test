# port/log — UART-адаптер логгера

Подключает `utils/log` к `bsp_uart_host`. Является эталонной реализацией
транспортного адаптера — при добавлении нового транспорта (Flash, USB CDC)
создаётся аналогичный каталог `port/log_flash/` по той же схеме.

---

## Использование

```c
#include "port/log_uart.h"
#include "log/log.h"

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_uart_host_init(115200U);

    log_uart_init();   // регистрирует транспорт, после этого LOG_* работают

    LOG_I("BOOT", "Ready");
}
```

Для **tft_app (FreeRTOS)** — добавить мьютекс до `log_uart_init()`:

```c
log_mutex_init();   // создать FreeRTOS-семафор
log_uart_init();    // зарегистрировать транспорт
```

Реализация мьютекса: `firmware/tft_app/src/log_mutex.c`.

---

## Что делает `log_uart_init()`

1. Регистрирует `bsp_uart_host_write()` как write callback через `log_init()`.
2. Предоставляет strong-реализацию `log_get_timestamp_ms()` → `bsp_tick_get_ms()`.

---

## CMake

```cmake
target_link_libraries(<target> PRIVATE port_log_uart)
```

Транзитивно подтягивает `utils` (содержит `log.h`) и `LOG_LEVEL`.
