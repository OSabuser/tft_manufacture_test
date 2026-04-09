# bsp_button — тактовые кнопки

Чтение двух тактовых кнопок SWT6x6 с программным debounce.
Предоставляет мгновенное сырое чтение, стабильное состояние и одноразовые
события нажатия/отпускания.

---

## Аппаратура

| Кнопка         | Пин MCU      | GPIO      | Схема                          | Нажатие |
|----------------|--------------|-----------|--------------------------------|---------|
| `BSP_BUTTON_1` | GPIO_B1_14   | GPIO2[30] | SWT6x6, pull-up к 3V3 внешний  | LOW     |
| `BSP_BUTTON_2` | GPIO_B1_15   | GPIO2[31] | SWT6x6, pull-up к 3V3 внешний  | LOW     |

Пины настроены в `BOARD_InitPins()` (`generated/pin_mux.c`) как INPUT с включённым
гистерезисом, без внутренней подтяжки (`0x0100B0`). `bsp_button_init()` не трогает
GPIO — только сбрасывает внутреннее состояние модуля.

---

## Принцип работы

```bash
GPIO2[30] / GPIO2[31]
        ↓
  GPIO_ReadPinInput()          ← вызывается в bsp_button_poll()
        ↓
  debounce (счётчик)           ← 4 одинаковых сэмпла подряд = 20 мс
        ↓
  stable state + события       ← evt_pressed / evt_released
        ↓
  bsp_button_is_pressed()
  bsp_button_get_event_pressed()
  bsp_button_get_event_released()
```

- **Polling** — `bsp_button_poll()` вызывается снаружи каждые 5 мс. Без прерываний.
- **Debounce** — счётчик подтверждений: 4 одинаковых сэмпла подряд фиксируют
  переход. Глитч (смена уровня до набора порога) сбрасывает счётчик.
- **События** — одноразовые флаги `evt_pressed` / `evt_released`, сбрасываются
  при первом обращении через `get_event_*()`.

Максимальная задержка реакции = период поллинга = 5 мс. Для навигации по меню
и производственного теста этого достаточно — порог восприятия задержки UI
около 50–100 мс.

---

## Быстрый старт

```c
#include "bsp/button.h"

/* После board_hw_init(): */
bsp_button_init();

/* bare-metal — вызывать каждые 5 мс из tick-коллбэка: */
bsp_button_poll();

/* В основном цикле: */
if (**bsp_button_get_event_pressed(BSP_BUTTON_1)**) {
    /* однократное срабатывание по нажатию */
}

if (bsp_button_is_pressed(BSP_BUTTON_2)) {
    /* кнопка удерживается */
}

/* FreeRTOS — в таске: */
vTaskDelay(pdMS_TO_TICKS(5));
bsp_button_poll();
if (bsp_button_get_event_pressed(BSP_BUTTON_1)) {
    xQueueSend(btn_queue, &btn_event, 0);
}
```

### Startup check (bootloader)

```c
/* Сырое чтение без debounce — сразу после board_hw_init(),
   до вызова bsp_button_init(). */
if (bsp_button_read(BSP_BUTTON_1)) {
    /* кнопка удерживается при старте → войти в режим обновления */
}
```

---

## API

### `bsp_button_init()`

Сбрасывает внутреннее состояние (счётчики debounce, флаги событий).
GPIO уже настроен в `BOARD_InitPins()` — вызывать после `board_hw_init()`.

### `bsp_button_read(btn)`

Мгновенное сырое чтение пина без debounce. Возвращает `true` если кнопка
нажата прямо сейчас. Предназначено для проверки при старте (bootloader hold-check).

### `bsp_button_poll()`

Один шаг debounce. Вызывать строго каждые 5 мс — из tick-коллбэка
(bare-metal) или таска (FreeRTOS). Обновляет стабильное состояние
и выставляет одноразовые события для обеих кнопок за один вызов.

### `bsp_button_is_pressed(btn)`

Стабильное состояние после debounce. `true` = кнопка удерживается нажатой.
Не сбрасывается при чтении.

### `bsp_button_get_event_pressed(btn)`

Одноразовое событие нажатия. Возвращает `true` один раз после того как
debounce зафиксировал переход в нажатое состояние. **Флаг сбрасывается при вызове.**

### `bsp_button_get_event_released(btn)`

Одноразовое событие отпускания. Возвращает `true` один раз после того как
debounce зафиксировал переход в отпущенное состояние. **Флаг сбрасывается при вызове.**

---

## Логика длинного/короткого нажатия

`bsp_button` намеренно не реализует логику длинного/короткого нажатия —
это интерпретация событий, зависящая от контекста приложения.

В `tft_app` реализуется отдельным модулем `button_handler` поверх событий BSP:

```bash
bsp_button  →  button_handler  →  app (меню, навигация)
```

`button_handler` хранит `press_start_ms`, использует `bsp_tick_get_ms()`
и вызывает коллбэк с типом действия (`SHORT_PRESS`, `LONG_PRESS`, `REPEAT`).
Не зависит от железа — тестируется на хосте как категория A (без fff).

---

## Конфигурация debounce

```c
/* bsp/button/src/button.c */
#define BUTTON_DEBOUNCE_SAMPLES 4U  /* 4 × 5 мс = 20 мс */
```

При изменении периода поллинга нужно пересчитать `BUTTON_DEBOUNCE_SAMPLES`
чтобы сохранить целевое время debounce (рекомендуется 15–30 мс).

---

## Тестирование

### Host unit-тесты

Категория **B** — `button.c` вызывает `GPIO_ReadPinInput()` из `fsl_gpio.h`.
SDK-функция мокируется через fff в тестовом файле.
Stub `fsl_gpio.h` уже существует в `tests/host/mocks/`.

```cmake
add_host_test(
    NAME    test_bsp_button
    SOURCES button/test_bsp_button.c
            ${CMAKE_SOURCE_DIR}/bsp/button/src/button.c
    INCLUDES
            ${CMAKE_SOURCE_DIR}/bsp/button/include
            ${CMAKE_SOURCE_DIR}/bsp/common/include
    MOCKS
            ${BSP_MOCKS_DIR}
)
```

Покрытие: инициализация, сырое чтение, debounce нажатия/отпускания,
потребление событий, сброс счётчика при глитче, независимость кнопок,
граничные значения индекса.

### HIL-тест (интерактивный)

Кнопки расположены на плате таргета — оператор нажимает вручную по подсказкам.

```bash
just host::hil-button    # запускать отдельно, не входит в hil-run
```

C-прошивка: `tests/target/hil_button/` — CLI через `bsp_uart_host`.
pytest: `tools/hil/04_test_button.py` — помечен `@pytest.mark.interactive`.

Команды CLI прошивки:

| Команда      | Ответ       | Описание                                   |
|--------------|-------------|--------------------------------------------|
| `PING`       | `PONG`      | Проверка канала                            |
| `READ <idx>` | `1` / `0`   | Сырое состояние (без debounce)             |
| `STATE <idx>`| `1` / `0`   | Стабильное состояние после debounce        |
| `EVENT_P <idx>`| `1` / `0` | `get_event_pressed`, сбрасывает флаг       |
| `EVENT_R <idx>`| `1` / `0` | `get_event_released`, сбрасывает флаг      |

---

## Зависимости

| Зависимость  | Тип     | Описание                                     |
|--------------|---------|----------------------------------------------|
| `bsp_status` | PUBLIC  | `bsp_status_t` в публичном API               |
| `bsp_board`  | PRIVATE | Транзитивно: `pin_mux.h`, clock, SDK headers |
| `sdk_gpio`   | PRIVATE | `fsl_gpio.h` — `GPIO_ReadPinInput()`         |

---

## Подключение

```cmake
# bsp/CMakeLists.txt
add_subdirectory(button)

# firmware/test/CMakeLists.txt или firmware/tft_app/CMakeLists.txt
target_link_libraries(firmware_test PRIVATE
    bsp_board
    bsp_tick
    bsp_button
)
```
