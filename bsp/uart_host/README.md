# bsp_uart_host

Коммуникационный канал с хост-машиной через LPUART1 (разъём J2, MCU-Link VCOM).

Применяется для HIL-тестов (pytest + pyserial), отладочного вывода и резервного
канала связи.

---

## Архитектура

```bash
[LPUART1 RX] → LPUART1_IRQHandler → ring_buffer_put()
                                           ↓
                              bsp_uart_host_read()   ← polling + таймаут
                              bsp_uart_host_read_byte()

[LPUART1 TX] ← bsp_uart_host_write()  ← LPUART_WriteBlocking()
```

- **TX** — blocking polling (`LPUART_WriteBlocking`). Пакеты короткие, задержка 1–2 мс приемлема.
- **RX** — ISR пишет в ring buffer, задача/main читает с таймаутом.
- **ISR** — `LPUART1_IRQHandler` определён в модуле, модуль владеет прерыванием целиком.
- **Singleton** — один экземпляр, один физический UART.

---

## Быстрый старт

```c
#include "bsp/uart_host.h"

// В main(), после board_hw_init():
bsp_uart_host_init(115200);

// TX
bsp_uart_host_write_str("hello\r\n");

// RX — ждать байт до 100 мс
int32_t byte = bsp_uart_host_read_byte(100);
if (byte < 0) { /* таймаут */ }

// RX — прочитать пакет целиком
uint8_t buf[64];
size_t n = bsp_uart_host_read(buf, sizeof(buf), 500);
```

---

## Конфигурация

Задаётся в CMakeLists.txt **firmware-таргета**, не модуля:

```cmake
target_compile_definitions(firmware_test PRIVATE
    BSP_UART_HOST_RX_BUFFER_SIZE=256   # степень двойки, дефолт 256
    BSP_UART_HOST_SRC_CLOCK_HZ=24000000
    BSP_UART_HOST_IRQ_PRIORITY=5
)
```

| Define                         | Дефолт     | Описание                                                                                                   |
| ------------------------------ | ---------- | ---------------------------------------------------------------------------------------------------------- |
| `BSP_UART_HOST_RX_BUFFER_SIZE` | `256`      | Размер RX ring buffer. **Должен быть степенью двойки.**                                                    |
| `BSP_UART_HOST_SRC_CLOCK_HZ`   | `24000000` | Частота источника тактирования LPUART1.                                                                    |
| `BSP_UART_HOST_IRQ_PRIORITY`   | `5`        | Приоритет `LPUART1_IRQn`. Должен быть ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY` при использовании FreeRTOS. |

---

## Таймауты

```c
// Без ожидания — вернёт только то, что уже есть в буфере
bsp_uart_host_read(buf, len, 0);

// Ждать с таймаутом (межбайтовый: сбрасывается после каждого принятого байта)
bsp_uart_host_read(buf, len, 100);

// Ждать вечно
bsp_uart_host_read(buf, len, BSP_UART_HOST_WAIT_FOREVER);
```

`bsp_uart_host_read()` возвращает `size_t` — частичное чтение при таймауте
не является ошибкой, caller сам решает что делать с полученным количеством байт.

---

## FreeRTOS

Модуль работает в FreeRTOS без отдельной реализации. При сборке с
`BSP_TICK_FREERTOS_MODE` в цикле ожидания добавляется `vTaskDelay(1)` —
задача отдаёт управление планировщику вместо busy-wait.

`BSP_UART_HOST_IRQ_PRIORITY` должен быть установлен ниже
`configMAX_SYSCALL_INTERRUPT_PRIORITY` (числовое значение выше).

---

## Подключение

```cmake
# bsp/CMakeLists.txt
add_subdirectory(common)
add_subdirectory(uart_host)

# firmware/test/CMakeLists.txt
target_link_libraries(firmware_test PRIVATE
    bsp_board
    bsp_tick
    bsp_uart_host
)
```

---

## Тестирование

Для host unit-тестов модуль предоставляет fff-заглушки через **Humble Object**:
в тестовой сборке вместо `uart_host.c` линкуется `mocks/uart_host_mock.c`.

```cmake
# tests/host/CMakeLists.txt
add_host_test(
  NAME
  uart_host_mock_example
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/uart_host/test_uart_host.c
  ${CMAKE_SOURCE_DIR}/bsp/uart_host/mocks/uart_host_mock.c
  # Если тестируете "protocol.c" который использует uart_host:
  # ${CMAKE_SOURCE_DIR}/bsp/protocol/src/protocol.c
  INCLUDES
  ${CMAKE_SOURCE_DIR}/bsp/uart_host/include # bsp/uart_host.h
  ${CMAKE_SOURCE_DIR}/bsp/uart_host/mocks # uart_host_mock.h
  ${CMAKE_SOURCE_DIR}/bsp/common/include # bsp/status.h
  # ${CMAKE_SOURCE_DIR}/bsp/protocol/include  # protocol.h
  MOCKS
  ${CMAKE_SOURCE_DIR}/bsp/uart_host/mocks/uart_host_mock.c)
```

```c
#include "fff.h"
DEFINE_FFF_GLOBALS;

#include "bsp/uart_host_mock.h"

void setUp(void) { UART_HOST_MOCK_RESET_ALL(); }

void test_something(void) {
    bsp_uart_host_write_fake.return_val = BSP_OK;
    // ... вызываем тестируемый код ...
    TEST_ASSERT_EQUAL(1, bsp_uart_host_write_fake.call_count);
}
```

---

## Зависимости

| Зависимость           | Тип     | Описание                          |
| --------------------- | ------- | --------------------------------- |
| `bsp_status`          | PUBLIC  | `bsp_status_t` в публичном API    |
| `bsp_tick`            | PRIVATE | `bsp_tick_get_ms()` для таймаутов |
| `utils` (ring_buffer) | PRIVATE | RX ring buffer                    |
| `sdk_lpuart`          | PRIVATE | `fsl_lpuart.h`, `fsl_clock.h`     |