# bsp_sdram — внешняя SDRAM MT48LC16M16A2 (32 МБ)

Подъём SEMC (для прошивок без DCD) и минимальная верификация доступности
внешней SDRAM, подключённой к SEMC. Подробное тестирование (паттерны, шина
адреса/данных, retention) выполняется в тест-модуле `firmware_test/test_sdram.c`,
который использует API этого модуля (путь с DCD, см. ниже).

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

---

## Контракт: кто поднимает SEMC

Два независимых пути, в зависимости от того, есть ли у прошивки DCD:

- **С DCD** (`firmware_test`, `bsp_boot_xip`): SEMC поднят DCD **до вызова
  `main()`**. Этот модуль в этом случае регистры SEMC не трогает — только
  `bsp_sdram_init()` для верификации.
- **Без DCD** (bootloader, `bsp_boot_xip_no_dcd`; в будущем `tft_app`): SEMC
  не поднимает никто, пока не будет явно вызван `bsp_sdram_configure()` —
  побитовый порт проверенной в производстве DCD-последовательности
  (`tools/host/dcd/dcd.bin`, «блок 2» — первый блок там мёртвый код,
  полностью перезаписывается вторым до какого-либо использования, поэтому
  не переносился). Источник истины — сам DCD, а не пересчёт по формулам SDK
  из наносекунд: значения регистров контроллера SEMC скопированы дословно.

**Почему `bsp_sdram_configure()` сама поднимает тактирование.** Штатный
`BOARD_BootClockRUN()` (его вызывает `board_hw_init()` в каждой прошивке) НЕ
настраивает PLL2 → PFD2 → делитель SEMC — этот блок в `clock_config.c`
выключен макросом `SKIP_SYSCLK_INIT`, который определён для **всех** таргетов
сборки (`bsp/CMakeLists.txt`), включая `bsp_boot_xip_no_dcd`. Смысл макроса —
не глитчить PLL, пока на нём уже висит поднятая DCD SDRAM (случай
`firmware_test`); но для пути без DCD это побочно означает, что тактирование
SEMC не настраивает вообще никто, кроме `bsp_sdram_configure()`. Результат —
SEMC ≈135.77 МГц (PLL2 528 МГц → PFD2 `FRAC=35` ≈271.54 МГц → `SEMC_PODF` ÷2),
под эту частоту тюнингованы все timing-регистры DCD.

Вызывать `bsp_sdram_configure()` в пути с DCD не нужно и не имеет смысла —
DCD уже сделала эту работу раньше, а `bsp_sdram_configure()` дублировала бы
её же на живой памяти.

---

## API

```c
bsp_status_t bsp_sdram_configure(void); /* поднять SEMC: тактирование → пины → контроллер → init-команды SDRAM */
bsp_status_t bsp_sdram_init(void);      /* верифицировать SDRAM (SEMC уже поднят — DCD либо bsp_sdram_configure()) */
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

**Поведение `bsp_sdram_configure()`:**

1. Тактирование: `CLOCK_InitSysPll()` (PLL2, 528 МГц) → `CLOCK_InitSysPfd()`
   (PFD2, `FRAC=35`) → `CLOCK_SetMux`/`CLOCK_SetDiv` (SEMC ← alt ← PFD2, ÷2).
2. IOMUX: пины `GPIO_EMC_00..41` → ALT0 (функция SEMC), `SW_PAD_CTL_PAD` =
   `0x000110F9`; `GPIO_EMC_39` (`SEMC_DQS`) дополнительно получает `SION`
   (иначе SEMC не сможет читать собственный read-strobe).
3. Регистры контроллера SEMC (`MCR`, `BMCR0/1`, `BR[0..8]`, `IOCR`,
   `SDRAMCR0..3`, `DBICR0/1`, `IPCR1/2`) — побитово из DCD.
4. Командная последовательность SDRAM через `SEMC_SendIPCommand()`:
   precharge-all → 2×auto-refresh → mode-set (`0x33` = BL8/sequential/CL3,
   согласуется с `SDRAMCR0` и даташитом MT48LC16M16A2) → включение
   авто-refresh (`SDRAMCR3 = 0x50210A09`).
5. AXI-QoS приоритеты SDRAM-мастеров (`0x41044100/104` LCD, `0x41442100/104`
   Cortex-M7 read/write_qos) — хвост DCD. Это не SEMC и не в заголовках
   `sdk/devices/MIMXRT1052` (NXP не заворачивает ARM'овский NIC-301 IP в
   CMSIS-структуру), но регистры реальные и документированы (i.MX RT1050 RM,
   гл. 29 "Network Interconnect Bus System (NIC-301)" — адреса сверены день-в-
   день: `0x41044100` = `GPV0_BASE(0x41000000)+0x44000+0x100` =
   `SIM_MAIN.LCD.read_qos`). Для bootloader инертны (нет конкуренции LCD/DMA
   vs CPU за шину), но входят в проверенную последовательность и понадобятся
   `tft_app`.

**Требует MPU Region 11** (`board_mpu_init()`, `bsp/generated/board.c`,
`0x41000000`, 8 МБ) — штатный Region 10 (периферия, только 4 МБ от
`0x40000000` = 4 домена AIPSTZ) NIC-301 GPV не покрывает; без Region 11 шаг 5
фолтит (deny-all errata-регион 0 перехватывает всё, что не покрыто более
специфичным регионом — `PRIVDEFENA` тут не спасает, т.к. регион 0 покрывает
весь диапазон 0x0..0xFFFFFFFF и потому всегда matched). DCD это переживает —
ROM пишет регистры до включения MPU. Уже добавлен в `board_mpu_init()`.

**Поведение `bsp_sdram_init()`:**

1. Ждёт перехода SEMC в IDLE (`SEMC->STS0 & SEMC_STS0_IDLE_MASK`),
   таймаут 10 мс.
2. Записывает `0xA5A5A5A5` по `BSP_SDRAM_TEST_BASE_ADDR`,
   делает `SCB_CleanDCache_by_Addr` + `SCB_InvalidateDCache_by_Addr` + `__DSB()`,
   читает обратно и сверяет.
3. Повторяет для инверсного паттерна `0x5A5A5A5A`.

**Коды возврата:**

| Функция                   | Код               | Условие                                     |
| -------------------------- | ----------------- | -------------------------------------------- |
| `bsp_sdram_configure()`    | `BSP_OK`          | Тактирование/пины/регистры/команды прошли    |
| `bsp_sdram_configure()`    | `BSP_ERR_INIT`    | IP-команда SEMC вернула ошибку (precharge/refresh/mode-set) |
| `bsp_sdram_init()`         | `BSP_OK`          | SDRAM доступна, оба паттерна совпали         |
| `bsp_sdram_init()`         | `BSP_ERR_TIMEOUT` | SEMC не перешёл в IDLE за 10 мс              |
| `bsp_sdram_init()`         | `BSP_ERR_INIT`    | Хотя бы один паттерн не совпал при readback  |

---

## Быстрый старт

```c
#include "bsp/sdram.h"

/* Путь С DCD (firmware_test) — SEMC уже поднят до main(): */
if (bsp_sdram_init() != BSP_OK) {
    handle_critical_error();
}

/* Путь БЕЗ DCD (bootloader smoke-test и т.п.) — сначала поднять SEMC сами: */
if (bsp_sdram_configure() != BSP_OK) {
    handle_semc_bringup_error();
}
if (bsp_sdram_init() != BSP_OK) {
    handle_sdram_error();
}

/* Работа с памятью по адресам внутри
   [BSP_SDRAM_BASE_ADDR, BSP_SDRAM_BASE_ADDR + BSP_SDRAM_SIZE_BYTES) */
```

---

## CMake

Сегодня линкует только `firmware_test` (путь с DCD). Для пути без DCD
потребитель (например, bootloader) добавляет зависимость сам — модуль
никого за собой не тянет:

```cmake
target_link_libraries(firmware_test PRIVATE bsp_sdram)
```

**Зависимости модуля:**

| Зависимость  | Тип     | Описание                                                        |
| ------------ | ------- | ----------------------------------------------------------------|
| `bsp_status` | PUBLIC  | `bsp_status_t` в публичном API                                  |
| `bsp_tick`   | PRIVATE | `bsp_tick_get_ms()` для таймаута SEMC IDLE                      |
| `sdk_semc`   | PRIVATE | `fsl_semc.h` — регистры `SEMC->`, `SEMC_SendIPCommand()`        |
| `bsp_board`  | PRIVATE | Транзитивно даёт `sdk_clock` (`CLOCK_Init*()`) и `sdk_device` (`IOMUXC`) — отдельных PRIVATE-строк на них не заводили |
