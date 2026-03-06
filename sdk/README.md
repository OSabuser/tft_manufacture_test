# Добавление и использование драйверов NXP SDK

Руководство по добавлению новых драйверов из SDK в проект и их использованию в BSP и firmware.

---

## Структура sdk/

```bash
sdk/
├── CMakeLists.txt                  # все SDK таргеты
├── CMSIS/
│   └── Include/
└── devices/
    └── MIMXRT1052/
        ├── system_MIMXRT1052.c
        ├── drivers/                # здесь живут все драйверы
        │   ├── fsl_flexcan.c / .h
        │   ├── fsl_lpuart.c / .h
        │   └── ...
        └── utilities/
```

---

## Как добавить новый драйвер

### Шаг 1 — Найти исходник драйвера

В SDK смотришь `devices/MIMXRT1052/drivers/`. Каждый драйвер — пара `.c` + `.h` файлов.

Если нужен FlexCAN — это `fsl_flexcan.c` + `fsl_flexcan.h`.

Для сложных драйверов (USDHC, SEMC) загляни в NXP-овский cmake файл рядом с драйвером — там указаны зависимости. Например файл `driver_flexcan.cmake` содержит:

```cmake
target_sources(... fsl_flexcan.c)
# зависит от driver_common
```

Это подскажет какие ещё таргеты нужно добавить в зависимости.

### Шаг 2 — Добавить таргет в sdk/CMakeLists.txt

Открываешь `sdk/CMakeLists.txt` и добавляешь через макрос `add_sdk_driver`:

```cmake
# Простой драйвер без дополнительных зависимостей
add_sdk_driver(flexcan fsl_flexcan.c)

# Драйвер с зависимостью от другого SDK таргета
add_sdk_driver(flexcan fsl_flexcan.c)
target_link_libraries(sdk_flexcan PUBLIC sdk_clock)
```

Макрос `add_sdk_driver(ИМЯ ФАЙЛ)` автоматически:

- Создаёт таргет `sdk_ИМЯ`
- Добавляет `fsl_ИМЯ.c` как источник
- Прокидывает `devices/MIMXRT1052/drivers` как PUBLIC include
- Линкует `sdk_device` как PUBLIC зависимость

### Шаг 3 — Использовать в BSP или firmware

**В BSP** — если драйвер нужен для инициализации платы (clock, gpio, common):

```cmake
# bsp/CMakeLists.txt
target_link_libraries(bsp_board PUBLIC
    sdk_device
    sdk_clock      # ← добавил
    sdk_common     # ← добавил
)
```

`PUBLIC` — транзитивно подтянется во все firmware таргеты которые линкуют `bsp_board`.

**В firmware** — если драйвер нужен только конкретному проекту:

```cmake
# firmware/test/CMakeLists.txt
target_link_libraries(firmware_test PRIVATE
    bsp_board      # транзитивно даёт sdk_device, sdk_clock, sdk_common
    sdk_flexcan    # ← добавил только здесь — нужен только тестовой прошивке
    sdk_lpuart
)
```

`PRIVATE` — драйвер линкуется только в этот бинарь, не экспортируется наружу.

---

## Справочник таргетов sdk/

| Таргет        | Файл                  | Когда нужен                          |
| ------------- | --------------------- | ------------------------------------ |
| `sdk_device`  | `system_MIMXRT1052.c` | Всегда (базовый)                     |
| `sdk_common`  | `fsl_common.c`        | Всегда (базовые утилиты)             |
| `sdk_clock`   | `fsl_clock.c`         | Всегда (тактирование)                |
| `sdk_gpio`    | `fsl_gpio.c`          | Кнопки, светодиоды, дискретные входы |
| `sdk_lpuart`  | `fsl_lpuart.c`        | UART1, RS_RX в режиме UART           |
| `sdk_flexcan` | `fsl_flexcan.c`       | CAN                                  |
| `sdk_usdhc`   | `fsl_usdhc.c`         | uSD слот                             |
| `sdk_semc`    | `fsl_semc.c`          | SDRAM (обычно не нужен — DCD)        |
| `sdk_flexspi` | `fsl_flexspi.c`       | QSPI Flash (обычно не нужен — FDCB)  |
| `sdk_lcdifv2` | `fsl_lcdifv2.c`       | LCD интерфейс                        |
| `sdk_pwm`     | `fsl_pwm.c`           | FlexPWM (VOLUME, подсветка)          |
| `sdk_adc`     | `fsl_adc.c`           | ADC                                  |

---

## Правила использования PUBLIC / PRIVATE

```bash
PUBLIC  — используй в bsp_board и sdk таргетах
          (зависимость экспортируется транзитивно)

PRIVATE — используй в firmware таргетах
          (зависимость только для этого бинаря)
```

**Пример транзитивной цепочки:**

```bash
firmware_test
    └── bsp_board (PRIVATE)
            ├── sdk_device (PUBLIC) ← подтягивается в firmware_test автоматически
            ├── sdk_clock  (PUBLIC) ← подтягивается в firmware_test автоматически
            └── sdk_common (PUBLIC) ← подтягивается в firmware_test автоматически
```

Благодаря этому в `firmware_test/CMakeLists.txt` не нужно явно прописывать `sdk_device`, `sdk_clock`, `sdk_common` — они приходят через `bsp_board`.

---

## Пример: добавить поддержку I2C (LPI2C) для BM8563

```cmake
# 1. sdk/CMakeLists.txt — добавляешь таргет
add_sdk_driver(lpi2c fsl_lpi2c.c)

# 2. bsp/CMakeLists.txt — если I2C нужен для инициализации платы
target_link_libraries(bsp_board PUBLIC sdk_lpi2c)

# ИЛИ firmware/app/CMakeLists.txt — если только в боевой прошивке
target_link_libraries(firmware_app PRIVATE sdk_lpi2c)
```
