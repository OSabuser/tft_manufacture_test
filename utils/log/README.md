# utils/log

Платформонезависимый логгер с callback-транспортом.

Ядро логгера (`log.c` / `log.h`) не знает о конкретном транспорте — UART,
USB CDC, Flash и т.д. Транспорт подключается через `log_init()` в виде
callback-функции. Адаптеры живут в `port/log/`.

| Параметр | Значение |
|---|---|
| Формат | `[  timestamp][L][TAG] сообщение\r\n` |
| Буфер строки | 256 байт (переопределяется через `LOG_BUF_SIZE`) |
| Управление уровнем | `LOG_LEVEL` через CMake `-DLOG_LEVEL=N` |
| Thread-safety | мьютекс через weak-хуки (`log_mutex_lock/unlock`) |
| Зависимости | `<stdarg.h>`, `<stdio.h>`, `<stddef.h>` |

## Уровни

| N | Макрос | Имя |
|---|--------|-----|
| 0 | — | off — все `LOG_*` → `((void)0)`, нулевой ROM |
| 1 | `LOG_E` | error |
| 2 | `LOG_W` | warn |
| 3 | `LOG_I` | info |
| 4 | `LOG_D` | debug |
| 5 | `LOG_V` | verbose |

По умолчанию: `VERBOSE` в Debug-сборке, `OFF` в Release (`NDEBUG`).

## Быстрый старт

```c
// main.c — зарегистрировать транспорт
#include "log/log.h"
#include "port/log_uart.h"

log_uart_init();          // инициализировать адаптер транспорта
log_init(uart_log_write, NULL);

// Любой .c файл
#include "log/log.h"

LOG_I("BOOT", "Started, tick=%lu", (unsigned long) bsp_tick_get_ms());
LOG_W("SDIO", "Card not detected");
LOG_D("UART", "RX=%u bytes", bsp_uart_host_rx_available());
LOG_E("CAN",  "Bus-off, err=%d", err);
```

Вывод:

```bash
[      1234][I][BOOT] Started, tick=1234
[      1235][W][SDIO] Card not detected
```

## Транспортный адаптер

Адаптер — функция типа `log_write_cb_t`:

```c
typedef void (*log_write_cb_t)(const char *p_buf, size_t len, void *p_ctx);
```

Готовые адаптеры в `port/log/`:

| Адаптер | Транспорт |
|---------|-----------|
| `log_uart` | `bsp_uart_host` (LPUART1, MCU-Link VCOM) |

## Мьютекс и временна́я метка (FreeRTOS)

Для bare-metal ничего делать не нужно — weak-хуки по умолчанию NOP, временна́я метка возвращает 0.

Для FreeRTOS переопределить в одном `.c` файле прошивки:

```c
// firmware/tft_app/src/log_os.c
#include "log/log.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "bsp/tick.h"

static SemaphoreHandle_t s_mutex;

void log_mutex_init(void)   { s_mutex = xSemaphoreCreateMutex(); }
void log_mutex_lock(void)   { xSemaphoreTake(s_mutex, portMAX_DELAY); }
void log_mutex_unlock(void) { xSemaphoreGive(s_mutex); }

uint32_t log_get_timestamp_ms(void) { return bsp_tick_get_ms(); }
```

> ⚠️ `LOG_*` нельзя вызывать из ISR — если callback транспорта блокирующий.

## Тесты

`tests/host/log/` — host unit-тесты (Unity + fff).
