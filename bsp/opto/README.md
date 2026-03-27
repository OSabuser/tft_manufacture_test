# bsp_opto — оптоизолированные входы

## Аппаратура

| Канал              | Пин MCU          | GPIO       | Схема                    |
|--------------------|------------------|------------|--------------------------|
| `BSP_OPTO_CH_IN1`  | GPIO_AD_B1_06    | GPIO1[22]  | PS2801-4 + 1K pull-up    |
| `BSP_OPTO_CH_IN2`  | GPIO_AD_B1_05    | GPIO1[21]  | PS2801-4 + 1K pull-up    |
| `BSP_OPTO_CH_RS`   | GPIO_AD_B1_07    | GPIO1[23]  | PS2801-4 + 1K pull-up    |

**Логика:** active-HIGH. Оптопары неинвертирующие (PS2801-4):

- Пин HIGH (ток есть) → `BSP_OPTO_STATE_ACTIVE`
- Пин LOW (тока нет)  → `BSP_OPTO_STATE_INACTIVE`

`BSP_OPTO_CH_RS` — опциональный. Активен только при `rs_as_gpio = true` в конфигурации.
При `rs_as_gpio = false` пин остаётся под управлением `bsp_uart_rs` (LPUART3_RX).

Все три пина принадлежат GPIO1[16..31] → один IRQ: `GPIO1_Combined_16_31_IRQn`.

---

## Режимы работы каналов

Каждый канал настраивается независимо через поле `modes[]` конфигурации.

### `BSP_OPTO_MODE_LEVEL` — детектирование уровня (IN1, IN2)

Предназначен для детектирования наличия/отсутствия сигнала с программным дебаунсом.

1. ISR фиксирует timestamp (`bsp_tick_get_ms()`) и raw состояние пина, взводит `pending`.
2. ISR **автоматически переключает направление прерывания** (RISING↔FALLING) после каждого
   фронта — оба края сигнала ловятся без дополнительной настройки.
3. `bsp_opto_process()` вызывается из main loop. Если с момента последнего фронта прошло
   >= `debounce_ms` — перечитывает пин, сравнивает с `confirmed_state`, вызывает коллбэк.

Начальный фронт выбирается **автоматически** при инициализации по текущему состоянию пина
(LOW → ждём RISING, HIGH → ждём FALLING). Поле `edges[]` для этого режима игнорируется.

Рекомендуемое значение `debounce_ms`: **10 мс** (PS2801-4 response ~50 мкс,
основной источник шума — механические контакты на стенде).

Коллбэк вызывается из контекста **main loop** (не из ISR).

### `BSP_OPTO_MODE_PROTO` — детектирование старт-бита протокола (RS)

Предназначен для приёма бинарных протоколов, где требуется минимальная задержка реакции
на первый фронт (старт-бит).

1. ISR фиксирует фронт и **немедленно вызывает коллбэк** — без дебаунса.
2. После срабатывания прерывание канала **отключается** автоматически.
3. Принимающий модуль (декодер протокола) после обработки пакета вызывает
   `bsp_opto_proto_arm()` чтобы взвести прерывание для следующего старт-бита.

Направление фронта задаётся полем `edges[]` и не меняется автоматически
(обычно `BSP_OPTO_EDGE_RISING` для старт-бита).

Коллбэк вызывается **прямо из ISR** — он должен быть ISR-safe:
только взводить флаг или писать в `volatile`-переменную, никакой бизнес-логики.

---

## Использование

### MODE_LEVEL (IN1, IN2)

```c
static void on_level_change(bsp_opto_ch_t ch, bsp_opto_state_t state)
{
    if (ch == BSP_OPTO_CH_IN1 && state == BSP_OPTO_STATE_ACTIVE) {
        /* IN1 активирован */
    }
}

bsp_opto_config_t cfg = {
    .callbacks   = { on_level_change, on_level_change, NULL },
    .modes       = { BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL },
    .edges       = { BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING },
    .rs_as_gpio  = false,
    .debounce_ms = 10U,
};
bsp_opto_init(&cfg);

/* в main loop: */
for (;;) {
    bsp_opto_process();
}
```

Полинг без коллбэков:

```c
bsp_opto_state_t state = bsp_opto_read(BSP_OPTO_CH_IN1);
```

### MODE_PROTO (RS) совместно с MODE_LEVEL (IN1, IN2)

```c
/* Коллбэк вызывается из ISR — только атомарные операции */
static volatile bool s_start_bit_detected = false;

static void on_rs_start_bit(bsp_opto_ch_t ch, bsp_opto_state_t state)
{
    s_start_bit_detected = true;
}

static void on_level_change(bsp_opto_ch_t ch, bsp_opto_state_t state) { /* ... */ }

bsp_opto_config_t cfg = {
    .callbacks   = { on_level_change, on_level_change, on_rs_start_bit },
    .modes       = { BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_PROTO },
    .edges       = { BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING },
    .rs_as_gpio  = true,
    .debounce_ms = 10U,
};
bsp_opto_init(&cfg);

/* в main loop: */
for (;;) {
    bsp_opto_process();   /* обслуживает IN1, IN2 */

    if (s_start_bit_detected) {
        s_start_bit_detected = false;
        /* запустить декодер протокола по таймеру... */

        /* по завершении приёма пакета — взвести для следующего старт-бита */
        bsp_opto_proto_arm(BSP_OPTO_CH_RS);
    }
}
```

---

## Совместное использование RS_RX

Пин GPIO_AD_B1_07 может работать в двух режимах:

| Режим          | BSP-модуль      | pin_mux функция         |
|----------------|-----------------|-------------------------|
| LPUART3 RX     | `bsp_uart_rs`   | `BOARD_InitRS_UART()`   |
| GPIO input     | `bsp_opto`      | `BOARD_InitRS_GPIO()`   |

Одновременно использовать оба нельзя. В `firmware_test` режим выбирается
при инициализации в зависимости от конфигурации теста.
