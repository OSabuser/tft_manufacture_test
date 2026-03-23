# bsp_opto — оптоизолированные входы

## Аппаратура

| Канал              | Пин MCU          | GPIO       | Схема                    |
|--------------------|------------------|------------|--------------------------|
| `BSP_OPTO_CH_IN1`  | GPIO_AD_B1_06    | GPIO1[22]  | PS2801-4 + 1K pull-up    |
| `BSP_OPTO_CH_IN2`  | GPIO_AD_B1_05    | GPIO1[21]  | PS2801-4 + 1K pull-up    |
| `BSP_OPTO_CH_RS`   | GPIO_AD_B1_07    | GPIO1[23]  | PS2801-4 + 1K pull-up    |

**Логика:** active-low. Пин HIGH = нет тока = `INACTIVE`. Пин LOW = ток течёт = `ACTIVE`.

`BSP_OPTO_CH_RS` — опциональный. Активен только при `rs_as_gpio = true` в конфигурации.
При `rs_as_gpio = false` пин остаётся под управлением `bsp_uart_rs` (LPUART3_RX).

Все три пина принадлежат GPIO1[16..31] → один IRQ: `GPIO1_Combined_16_31_IRQn`.

## Дебаунс

Аппаратного фильтра нет. Реализован программный дебаунс:

1. ISR фиксирует timestamp (`bsp_tick_get_ms()`) и raw состояние пина, выставляет `pending`.
2. `bsp_opto_process()` вызывается из main loop. Если с момента последнего фронта прошло
   >= `debounce_ms` — перечитывает пин, сравнивает с `confirmed_state`, стреляет коллбэком.

Рекомендуемое значение `debounce_ms`: **10 мс** (PS2801-4 response ~50 мкс,
основной источник шума — механические контакты на стенде).

## Использование

```c
static void on_opto_change(bsp_opto_ch_t ch, bsp_opto_state_t state)
{
    if (ch == BSP_OPTO_CH_IN1 && state == BSP_OPTO_STATE_ACTIVE) {
        /* IN1 активирован */
    }
}

bsp_opto_config_t cfg = {
    .callbacks    = { on_opto_change, on_opto_change, NULL },
    .rs_as_gpio   = false,
    .debounce_ms  = 10U,
};
bsp_opto_init(&cfg);

/* в main loop: */
for (;;) {
    bsp_opto_process();
    /* ... */
}
```

Полинг без коллбэков:

```c
bsp_opto_state_t state = bsp_opto_read(BSP_OPTO_CH_IN1);
```

## Совместное использование RS_RX

Пин GPIO_AD_B1_07 может работать в двух режимах:

| Режим          | BSP-модуль      | pin_mux функция         |
|----------------|-----------------|-------------------------|
| LPUART3 RX     | `bsp_uart_rs`   | `BOARD_InitRS_UART()`   |
| GPIO input     | `bsp_opto`      | `BOARD_InitRS_GPIO()`   |

Одновременно использовать оба нельзя. В `firmware_test` режим выбирается
при инициализации в зависимости от конфигурации теста.
