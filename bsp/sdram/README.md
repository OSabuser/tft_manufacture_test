# bsp_sdram — внешняя SDRAM MT48LC16M16A2 (32 МБ)

> Расположение: `bsp/sdram/`
> Публичный заголовок: `bsp/sdram/include/bsp/sdram.h`
> Реализация: `bsp/sdram/src/sdram.c`

Модуль обеспечивает минимальную верификацию доступности внешней SDRAM,
подключённой к SEMC. Подробное тестирование (паттерны, шина адреса/данных,
retention) выполняется не здесь, а в тест-модуле `firmware_test/test_sdram.c`,
который опирается на константы и API этого модуля.

---

## Аппаратный контекст

| Параметр              | Значение                                         |
| --------------------- | ------------------------------------------------ |
| Чип                   | MT48LC16M16A2                                    |
| Объём                 | 32 МБ                                            |
| Ширина шины данных    | 16 бит                                           |
| Интерфейс MCU         | SEMC, регион BR0                                 |
| Базовый адрес         | `0x80000000` (`BSP_SDRAM_BASE_ADDR`)             |
| Конец региона         | `0x81FFFFFF` (`+ BSP_SDRAM_SIZE_BYTES = 32 МБ`)  |

Карта тестового региона (из `sdram.h`):

| Адрес        | Назначение                                                 |
| ------------ | ---------------------------------------------------------- |
| `0x80000000` | Начало SDRAM (SEMC BR0)                                    |
| `0x80200000` | `BSP_SDRAM_TEST_BASE_ADDR` — база тестового региона        |
| `0x81E00000` | Начало non-cacheable региона (USB DMA, 2 MB)               |
| `0x81FFFFFF` | Конец SDRAM                                                |

Тестовая база смещена на 2 МБ от начала SDRAM, что согласно комментариям в
заголовке гарантированно выше `.data`/`.bss` прошивки и ниже non-cacheable
региона.

---

## Архитектурное ограничение: SEMC инициализируется DCD до `main()`

Модуль **не настраивает** контроллер SEMC и не модифицирует его регистры.
Согласно комментарию в `bsp/sdram/CMakeLists.txt` и `sdram.h`, инициализация
SEMC выполнена через **DCD до вызова `main()`**. Регион SDRAM также описан в
MPU как Normal Write-Back cacheable — соответствующая настройка делается за
пределами этого модуля (исходники модуля её не выполняют).

Следствие для верификации: чтобы проверить именно физическую SDRAM, а не
кэш, при readback в реализации используется явный
`SCB_CleanDCache_by_Addr` + `SCB_InvalidateDCache_by_Addr` + `__DSB()`.

---

## Состав модуля

```
bsp/sdram/
├── include/bsp/sdram.h     # публичный заголовок
├── src/sdram.c             # реализация bsp_sdram_init()
└── CMakeLists.txt          # цель bsp_sdram
```

В `sdram.c` определены только статические вспомогательные функции
(`wait_semc_idle`, `flush_cache_at_test_base`, `verify_word`) и одна публичная
функция `bsp_sdram_init()`. Других публичных операций (например, расширенных
тестов памяти) модуль не предоставляет.

---

## Публичные константы

```c
#define BSP_SDRAM_BASE_ADDR          0x80000000UL  /* SEMC BR0           */
#define BSP_SDRAM_SIZE_BYTES         0x02000000UL  /* 32 MB              */
#define BSP_SDRAM_TEST_BASE_ADDR     0x80200000UL  /* +2 MB от базы       */
#define BSP_SDRAM_TEST_FAST_SIZE     0x00010000UL  /* 64 KB              */
#define BSP_SDRAM_TEST_FULL_SIZE     0x00100000UL  /* 1 MB               */
#define BSP_SDRAM_TEST_EXTENDED_SIZE 0x01B00000UL  /* 27 MB              */
```

Размеры тестов — это **константы для потребителей**; сам `bsp_sdram` не
запускает по ним внутренние проходы. Например, `BSP_SDRAM_TEST_FAST_SIZE`
используется в `firmware_test/test_sdram.c` (фаза «data bus»). Константы
`BSP_SDRAM_TEST_FULL_SIZE` и `BSP_SDRAM_TEST_EXTENDED_SIZE` определены в
заголовке, но их использование текущими потребителями в дереве не описано —
рассматривайте их как ориентиры из описания карты памяти.

---

## Публичный API

```c
bsp_status_t bsp_sdram_init(void);
```

Назначение: верифицировать, что SEMC завершил инициализацию (выполненную DCD)
и что SDRAM отвечает по тестовому адресу.

Поведение (из `sdram.c`):

1. Ждёт перехода SEMC в состояние IDLE по флагу `SEMC->STS0 & SEMC_STS0_IDLE_MASK`,
   таймаут — `SDRAM_SEMC_IDLE_TIMEOUT_MS = 10 мс` (через `bsp_tick_get_ms()`).
2. Записывает по `BSP_SDRAM_TEST_BASE_ADDR` паттерн `0xA5A5A5A5`,
   делает `SCB_CleanDCache_by_Addr` + `SCB_InvalidateDCache_by_Addr` + `__DSB()`,
   читает обратно и сверяет.
3. Повторяет то же для инверсного паттерна `0x5A5A5A5A`.
4. При успехе устанавливает внутренний флаг готовности и возвращает `BSP_OK`.

Коды возврата:

| Код               | Когда                                                                 |
| ----------------- | --------------------------------------------------------------------- |
| `BSP_OK`          | SDRAM доступна, оба паттерна успешно прочитаны обратно.               |
| `BSP_ERR_TIMEOUT` | SEMC не перешёл в IDLE за `SDRAM_SEMC_IDLE_TIMEOUT_MS` (10 мс).       |
| `BSP_ERR_INIT`    | Хотя бы один паттерн не совпал при readback (DCD/SDRAM не готовы).    |

`bsp_sdram_init()` затрагивает только 4 байта по адресу
`BSP_SDRAM_TEST_BASE_ADDR` (две записи 32-битных слов) и не пересекается с
`.data`/`.bss` прошивки благодаря смещению на 2 МБ от базы.

---

## Порядок использования

```c
#include "bsp/sdram.h"

if (bsp_sdram_init() != BSP_OK) {
    /* SEMC/SDRAM недоступны — это критическая ошибка для прошивки,
       которая использует SDRAM под фреймбуферы и тестовые регионы. */
    handle_critical_error();
}

/* Дальше — обычная работа с памятью по адресам внутри
   [BSP_SDRAM_BASE_ADDR, BSP_SDRAM_BASE_ADDR + BSP_SDRAM_SIZE_BYTES). */
```

Полноценные тесты памяти (шина адреса, шина данных, sequential, retention)
запускаются отдельным тест-модулем — см. раздел «Связь с firmware_test».

---

## Зависимости и CMake

```cmake
# bsp/sdram/CMakeLists.txt
add_library(bsp_sdram STATIC src/sdram.c)

target_include_directories(bsp_sdram
    PUBLIC  include/
    PRIVATE src/)

target_link_libraries(bsp_sdram
    PUBLIC  bsp_status
    PRIVATE bsp_board bsp_tick sdk_semc)
```

- `bsp_status` (PUBLIC) — `bsp_status_t` в публичном API.
- `bsp_tick` (PRIVATE) — `bsp_tick_get_ms()` для таймаута SEMC IDLE.
- `sdk_semc` (PRIVATE) — `fsl_semc.h`, нужен для `SEMC->STS0` и
  `SEMC_STS0_IDLE_MASK` при проверке готовности контроллера.
- `bsp_board` (PRIVATE) — общие board-уровневые символы.

Цель не собирается при `BUILD_TESTS_HOST=ON` (host-сборка), `CMakeLists.txt`
содержит ранний `return()`.

Потребитель (пример из `firmware/test/CMakeLists.txt`):

```cmake
target_link_libraries(firmware_test PRIVATE
    ...
    bsp_sdram
    ...
)
```

---

## Связь с firmware_test (`test_sdram.c`)

Тест-модуль `firmware/test/src/tests/test_sdram.c` использует этот BSP как
основу:

- В `init`-фазе модуль вызывает `bsp_sdram_init()` и сохраняет результат
  в `g_s_ready`. При неуспехе `run` сразу возвращает FAIL с
  `detail = "SEMC not ready — DCD failed?"`.
- Базовый адрес тестового региона берётся из `BSP_SDRAM_TEST_BASE_ADDR`.
- Размер фазы «data bus» — `BSP_SDRAM_TEST_FAST_SIZE` (64 KB).
- Остальные фазы (`address bus`, `sequential`, `retention`) используют
  свои локальные константы (`SDRAM_ADDR_BUS_BITS`, `SDRAM_SEQUENTIAL_SIZE`,
  `SDRAM_RETENTION_SIZE`), определённые в `test_sdram.c`, не в BSP.
- Cache maintenance в тестовом модуле повторяет ту же схему, что в BSP:
  `SCB_CleanDCache_by_Addr` + `SCB_InvalidateDCache_by_Addr` + `__DSB()`.

Дескриптор тест-модуля:

```c
const test_module_t K_TEST_SDRAM = {
    .id           = "sdram",
    .name         = "SDRAM 32 MB",
    .critical     = true,
    .requires_hil = false,
    ...
};
```

---

## Ограничения и замечания

- Модуль **не выполняет** инициализацию SEMC, DCD или MPU. Если соответствующие
  механизмы не отработали до `main()`, `bsp_sdram_init()` вернёт ошибку, но
  починить ситуацию из этого модуля нельзя — корень проблемы в DCD / startup
  / clock-config.
- Реентрантность `bsp_sdram_init()` не описана и не гарантируется: в прошивке
  вызов выполняется однократно на этапе инициализации.
- Тайм-аут IDLE (`10 мс`) рассчитан на здоровый контроллер; в случае реального
  отказа SEMC именно эта величина определяет, через сколько `BSP_ERR_TIMEOUT`
  будет возвращён.
- Cache maintenance в `verify_word()` работает только по одной кэш-линии
  (32 байта), и `BSP_SDRAM_TEST_BASE_ADDR = 0x80200000` подобран кратным
  размеру кэш-линии Cortex-M7 — иначе вызовы `SCB_*_by_Addr` потребовали бы
  выравнивания.
- Связь между константами размеров (`BSP_SDRAM_TEST_FULL_SIZE`,
  `BSP_SDRAM_TEST_EXTENDED_SIZE`) и конкретными сценариями тестирования не
  гарантируется этим BSP — это значения из карты памяти, которые потребитель
  может использовать или игнорировать. Точная стратегия тестов SDRAM
  определена в `firmware_test/test_sdram.c`.
