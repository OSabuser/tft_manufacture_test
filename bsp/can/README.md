# bsp_can

Приём и передача CAN 2.0 фреймов через FlexCAN2 (трансивер SN65HVD230D, разъём P1 контакты 3–4).

Применяется для обмена с управляющими модулями по CAN-шине: приём команд,
отправка откликов. Протоколы на стороне приложения — разный DLC, STD/EXT ID.

---

## Архитектура

```bash
                        bsp_can_send()          ← blocking polling + таймаут
                              ↓
[FlexCAN2 TX MB1] → SN65HVD230D → CAN bus

[CAN bus] → SN65HVD230D → [FlexCAN2 RX MB2..17]
                                    ↓
                        poll_rx_mailboxes()      ← опрос флагов MB
                                    ↓
                              ring_buffer         ← внутренний FIFO
                                    ↓
                        bsp_can_receive()         ← polling + таймаут
```

- **TX** — blocking polling с таймаутом. `bsp_can_send()` записывает фрейм
  в TX MB и ждёт флага завершения. Worst case при 500 kbit/s — ~260 мкс на фрейм.
- **RX** — polling. `bsp_can_receive()` опрашивает все активные RX MB,
  складывает найденные фреймы во внутренний ring buffer, возвращает первый
  доступный. Без прерываний.
- **Singleton** — один экземпляр, одна CAN-шина.

---

## Распределение Message Buffers

| MB    | Назначение                                          |
|-------|-----------------------------------------------------|
| 0     | Зарезервирован (ERR005829 workaround: inactive TX)  |
| 1     | TX — отправка фреймов                               |
| 2..17 | RX — до 16 индивидуальных фильтров                  |

ERR005829 — errata FlexCAN на i.MX RT1050/1052: при гонке TX/RX арбитража
MB 0 может зависнуть. Workaround: MB 0 всегда `kFLEXCAN_TxMbInactive`.

---

## Быстрый старт

```c
#include "bsp/can.h"

/* После board_hw_init() + bsp_tick_init(): */
bsp_can_config_t cfg = { .bitrate = 500000U };
bsp_can_init(&cfg);

/* Принимать всё */
bsp_can_accept_all();

/* TX — blocking, таймаут 500 мс */
bsp_can_frame_t tx = {
    .id = 0x123, .dlc = 4, .is_extended = false,
    .data = {0xDE, 0xAD, 0xBE, 0xEF}
};
bsp_can_send(&tx, 500);

/* RX — polling, таймаут 100 мс */
bsp_can_frame_t rx;
if (bsp_can_receive(&rx, 100) == BSP_OK) {
    /* обработать rx.id, rx.data[0..rx.dlc-1] */
}
```

---

## Фильтрация

Каждый фильтр занимает один RX MB. Максимум 16 фильтров (`BSP_CAN_FILTER_MAX`).

```c
/* Принимать только STD ID 0x200 с точным совпадением */
bsp_can_set_filter(0, 0x200, 0x7FF, false);

/* Принимать STD ID 0x300..0x30F (маска 0x7F0, младшие 4 бита игнорируются) */
bsp_can_set_filter(1, 0x300, 0x7F0, false);

/* Принимать EXT ID 0x1ABCDEF0 с точным совпадением */
bsp_can_set_filter(2, 0x1ABCDEF0, 0x1FFFFFFF, true);

/* Сбросить фильтры — принимать всё (STD + EXT) */
bsp_can_accept_all();
```

`bsp_can_accept_all()` настраивает два MB: один для STD (маска 0), один
для EXT (маска 0). Все остальные MB деактивируются.

---

## Блокирующее поведение

### TX: `bsp_can_send()`

Блокирующий вызов. Записывает фрейм в TX MB через `FLEXCAN_WriteTxMb()`,
затем ждёт флага завершения с polling `FLEXCAN_GetMbStatusFlags()`.
Возвращается по одному из условий:

| Условие | Возврат |
|---------|---------|
| Фрейм успешно отправлен | `BSP_OK` |
| TX MB занят предыдущей передачей | `BSP_ERR_BUSY` |
| Истёк `timeout_ms` | `BSP_ERR_TIMEOUT` |
| Невалидные параметры | `BSP_ERR_PARAM` |

При 500 kbit/s максимальное время отправки одного фрейма — ~260 мкс.
Для bare-metal и FreeRTOS-задачи это приемлемо.

### RX: `bsp_can_receive()`

Polling с таймаутом. Обходит все активные RX MB, читает готовые фреймы
во внутренний ring buffer, пытается извлечь один фрейм:

| Условие | Возврат |
|---------|---------|
| Фрейм найден (из буфера или MB) | `BSP_OK` |
| Истёк `timeout_ms` | `BSP_ERR_TIMEOUT` |
| Невалидные параметры | `BSP_ERR_PARAM` |

```c
/* Неблокирующий опрос — timeout_ms = 0 */
if (bsp_can_receive(&rx, 0) == BSP_OK) { /* есть фрейм */ }

/* Ожидание с таймаутом */
bsp_can_receive(&rx, 1000);  /* ждать до 1 секунды */
```

---

## Callback-механизм (заглушка)

В первой итерации `bsp_can_register_rx_callback()` возвращает
`BSP_ERR_NOT_SUPPORTED`. API заложен для будущей интеграции с FreeRTOS:

```c
/* Будущее использование в firmware/tft_app: */
static void can_isr_to_queue(const bsp_can_frame_t *p_frame, void *p_ctx)
{
    /* xQueueSendFromISR(...) */
}

bsp_can_register_rx_callback(can_isr_to_queue, NULL);
```

При реализации callback включит прерывания на RX MB. ISR читает фрейм
и вызывает callback напрямую. Callback **не должен блокироваться** — только
атомарные операции (флаг, очередь). Polling через `bsp_can_receive()`
отключается при активном callback.

---

## FreeRTOS

Модуль не зависит от FreeRTOS и работает в обоих контекстах:

| Контекст | TX | RX |
|----------|----|----|
| bare-metal (`firmware/test`, HIL) | `bsp_can_send()` — blocking polling | `bsp_can_receive()` — polling |
| FreeRTOS (`firmware/tft_app`) | `bsp_can_send()` — из задачи | `bsp_can_receive()` — из задачи с `timeout_ms` |

Для FreeRTOS с минимальной латентностью — будущий callback + `xQueueSendFromISR()`.
Polling с `timeout_ms = 10` из задачи подходит для протоколов с интервалом > 10 мс.

---

## Подключение

```cmake
# bsp/CMakeLists.txt
add_subdirectory(can)

# firmware/test/CMakeLists.txt
target_link_libraries(firmware_test PRIVATE
    bsp_board
    bsp_tick
    bsp_can
)
```

---

## Тестирование

### Host unit-тесты (тестирование логики bsp_can)

Категория **B** — модуль вызывает NXP SDK (`fsl_flexcan.h`).
SDK-функции мокаются через fff в тестовом файле.
Stubs: `fsl_flexcan.h`, `fsl_common.h`, `clock_config.h` в `tests/host/mocks/`.

```cmake
add_host_test(
    NAME    test_bsp_can
    SOURCES can/test_bsp_can.c
            ${PROJECT_SOURCE_DIR}/bsp/can/src/bsp_can.c
            ${PROJECT_SOURCE_DIR}/utils/ring_buffer/ring_buffer.c
    INCLUDES
            ${PROJECT_SOURCE_DIR}/bsp/can/include
            ${PROJECT_SOURCE_DIR}/bsp/common/include
            ${PROJECT_SOURCE_DIR}/utils/ring_buffer
    MOCKS
            ${BSP_MOCKS_DIR}
)
```

### Humble Object (тестирование потребителей bsp_can)

Модуль предоставляет fff-заглушки в `bsp/can/mocks/`:

```c
#include "fff.h"
DEFINE_FFF_GLOBALS;

#include "bsp/can.h"
#include "can_mock.h"

void setUp(void) { CAN_MOCK_RESET_ALL(); }

void test_protocol_sends_response(void) {
    bsp_can_send_fake.return_val = BSP_OK;
    /* ... вызываем protocol_handle_frame() ... */
    TEST_ASSERT_EQUAL(1, bsp_can_send_fake.call_count);
}
```

### HIL-тесты

C-прошивка `tests/target/can/` с CLI через UART + pytest `tools/hil/test_can.py`.
CAN-адаптер на стороне хоста — M5Stack с CAN-модулем.

---

## Зависимости

| Зависимость | Тип | Описание |
|-------------|-----|----------|
| `bsp_status` | PUBLIC | `bsp_status_t` в публичном API |
| `bsp_tick` | PRIVATE | `bsp_tick_get_ms()` для таймаутов |
| `ring_buffer` | PRIVATE | Внутренний RX FIFO |
| `sdk_flexcan` | PRIVATE | `fsl_flexcan.h` — FlexCAN2 SDK драйвер |
| `clock_config` | PRIVATE | `BOARD_BOOTCLOCKRUN_CAN_CLK_ROOT` |
