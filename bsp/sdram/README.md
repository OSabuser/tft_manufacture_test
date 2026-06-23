# bsp_sdram — внешняя SDRAM MT48LC16M16A2 (32 МБ)

Минимальная верификация доступности внешней SDRAM, подключённой к SEMC.
Подробное тестирование (паттерны, шина адреса/данных, retention) выполняется
в тест-модуле `firmware_test/test_sdram.c`, который использует константы
и API этого модуля.

---

## Аппаратура

| Параметр           | Значение                             |
| ------------------ | ------------------------------------ |
| Чип                | MT48LC16M16A2                        |
| Объём              | 32 МБ                                |
| Ширина шины данных | 16 бит                               |
| Интерфейс MCU      | SEMC, регион BR0                     |
| Базовый адрес      | `0x80000000` (`BSP_SDRAM_BASE_ADDR`) |

**Карта тестового региона:**

| Адрес        | Назначение                                          |
| ------------ | --------------------------------------------------- |
| `0x80000000` | Начало SDRAM (SEMC BR0)                             |
| `0x80200000` | `BSP_SDRAM_TEST_BASE_ADDR` — база тестового региона |
| `0x81E00000` | Начало non-cacheable региона (USB DMA, 2 MB)        |
| `0x81FFFFFF` | Конец SDRAM                                         |

Тестовая база смещена на 2 МБ от начала — гарантированно выше `.data`/`.bss`
прошивки и ниже non-cacheable региона.

**Важно:** SEMC инициализируется через DCD **до вызова `main()`**. Этот модуль
не настраивает SEMC и не трогает его регистры. Если DCD не отработал —
`bsp_sdram_init()` вернёт ошибку, но исправить ситуацию из модуля нельзя.

---

## API

```c
bsp_status_t bsp_sdram_init(void);
```

**Публичные константы:**

```c
#define BSP_SDRAM_BASE_ADDR          0x80000000UL  /* SEMC BR0  */
#define BSP_SDRAM_SIZE_BYTES         0x02000000UL  /* 32 MB     */
#define BSP_SDRAM_TEST_BASE_ADDR     0x80200000UL  /* +2 MB     */
#define BSP_SDRAM_TEST_FAST_SIZE     0x00010000UL  /* 64 KB     */
#define BSP_SDRAM_TEST_FULL_SIZE     0x00100000UL  /* 1 MB      */
#define BSP_SDRAM_TEST_EXTENDED_SIZE 0x01B00000UL  /* 27 MB     */
```

Константы размеров — для потребителей; `bsp_sdram` не запускает по ним
внутренних проходов.

**Поведение `bsp_sdram_init()`:**

1. Ждёт перехода SEMC в IDLE (`SEMC->STS0 & SEMC_STS0_IDLE_MASK`),
   таймаут 10 мс.
2. Записывает `0xA5A5A5A5` по `BSP_SDRAM_TEST_BASE_ADDR`,
   делает `SCB_CleanDCache_by_Addr` + `SCB_InvalidateDCache_by_Addr` + `__DSB()`,
   читает обратно и сверяет.
3. Повторяет для инверсного паттерна `0x5A5A5A5A`.

**Коды возврата:**

| Код               | Условие                                     |
| ----------------- | ------------------------------------------- |
| `BSP_OK`          | SDRAM доступна, оба паттерна совпали        |
| `BSP_ERR_TIMEOUT` | SEMC не перешёл в IDLE за 10 мс             |
| `BSP_ERR_INIT`    | Хотя бы один паттерн не совпал при readback |

---

## Быстрый старт

```c
#include "bsp/sdram.h"

if (bsp_sdram_init() != BSP_OK) {
    handle_critical_error();
}

/* Работа с памятью по адресам внутри
   [BSP_SDRAM_BASE_ADDR, BSP_SDRAM_BASE_ADDR + BSP_SDRAM_SIZE_BYTES) */
```

---

## CMake

```cmake
target_link_libraries(firmware_test PRIVATE bsp_sdram)
```

**Зависимости модуля:**

| Зависимость  | Тип     | Описание                                           |
| ------------ | ------- | -------------------------------------------------- |
| `bsp_status` | PUBLIC  | `bsp_status_t` в публичном API                     |
| `bsp_tick`   | PRIVATE | `bsp_tick_get_ms()` для таймаута SEMC IDLE         |
| `sdk_semc`   | PRIVATE | `fsl_semc.h` — `SEMC->STS0`, `SEMC_STS0_IDLE_MASK` |
| `bsp_board`  | PRIVATE | Общие board-уровневые символы                      |
