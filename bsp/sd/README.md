# bsp_sd — SD host-контроллер (USDHC1)

Модуль инициализирует SD host-контроллер и проверяет наличие карты.
Файловой системой не занимается — это ответственность `bsp_usd` (поверх) или
приложения напрямую.

## Место в архитектуре

Каждый слой знает только о слое ниже — зависимости не пересекают границы.

```bash
firmware_test / tft_app
│
▼
bsp_usd                  ← монтирование FatFS, тест R/W
│
├──► firmware_test_fatfs   ← ff.c + fsl_sd_disk + diskio (bare-metal ffconf)
│    tft_app_fatfs         ← ff.c + fsl_sd_disk + diskio (FreeRTOS ffconf)
│         │
│         ▼
│    port/fatfs/sd         ← diskio_sd.c: microsd_disk_* → fsl_sd_disk
│         │
▼         ▼
bsp_sd                   ← этот модуль: host init / deinit / card detect
│
▼
bsp/generated/sdmmc_config   ← board-level: BOARD_SD_Config, GPIO питания, pad config
│
▼
sdk_sdmmc_sd                 ← NXP: fsl_sd, fsl_sdmmc_common, fsl_sdmmc_host (non-blocking)
│
▼
sdk_usdhc                    ← NXP HAL: fsl_usdhc

```

---

## Аппаратный контекст

| Сигнал   | Пин MCU         | Конфигурация                                      |
|----------|-----------------|---------------------------------------------------|
| CLK      | GPIO_SD_B0_01   | USDHC1_CLK, периферийный режим                    |
| CMD      | GPIO_SD_B0_00   | USDHC1_CMD, периферийный режим                    |
| D0–D3    | GPIO_SD_B0_02–05| USDHC1_DATA0–3, периферийный режим                |
| CD_B     | GPIO_B1_12      | USDHC1_CD_B — детект через USDHC PRSSTAT          |
| SdPwr    | GPIO_AD_B1_03   | GPIO1[19], active-high, управляется SDK через BSP |

**CD_B** подключён как периферийный сигнал USDHC1, а не как GPIO. Детект карты
читается через `USDHC_GetPresentStatusFlags` → `kUSDHC_CardInsertedFlag`.
GPIO-прерывание на CD не используется (`kSD_DetectCardByHostCD`).

**SdPwr** инициализируется в `BOARD_SD_Config()` как GPIO-выход, выключен при старте.
SDK включает питание автоматически в процессе `SD_HostInit()` через callback.

---

## API

### `bsp_sd_init(void)`

Конфигурирует SDMMC host однократно (`BOARD_SD_Config`) и запускает
host-контроллер (`SD_HostInit`).

Повторный вызов без `bsp_sd_deinit` — no-op, возвращает `BSP_OK`.

Возвращает:

- `BSP_OK` — host готов к работе;
- `BSP_ERR_INIT` — `SD_HostInit` вернул ошибку.

### `bsp_sd_deinit(void)`

Останавливает host-контроллер и отключает питание карты.
Безопасен при вызове до `init` или повторно после `deinit`.

Возвращает:

- `BSP_OK` — всегда.

### `bsp_sd_is_inserted(void)`

Читает регистр `USDHC1 PRSSTAT`. Не требует предварительного `bsp_sd_init()` —
включает тактирование USDHC1 самостоятельно через `CLOCK_EnableClock`.

Возвращает:

- `true`  — карта вставлена;
- `false` — карта отсутствует.

---

## Разделение ответственности: bsp_sd vs sdmmc_config vs port_fatfs_sd

| Слой                  | Что делает                                              | Где живёт              |
|-----------------------|---------------------------------------------------------|------------------------|
| `sdmmc_config`        | Константы платы, `BOARD_SD_Config`, GPIO питания, pads | `bsp/generated/`       |
| `bsp_sd`              | `SD_HostInit/Deinit`, идемпотентность, card detect     | `bsp/sd/`              |
| `port_fatfs_sd`       | `microsd_disk_*` → `fsl_sd_disk` (FatFS diskio glue)   | `port/fatfs/sd/`       |
| `firmware_test_fatfs` | `ff.c` + `fsl_sd_disk` + `diskio.c` (bare-metal ffconf)| `firmware/test/fatfs/` |
| `tft_app_fatfs`       | `ff.c` + `fsl_sd_disk` + `diskio.c` (FreeRTOS ffconf)  | `firmware/tft_app/fatfs/` (будущее) |

**Почему `ff.c` и `fsl_sd_disk.c` не компилируются один раз как общая библиотека:**
оба включают `ff.h` → `ffconf.h`, который разный для `firmware_test` (bare-metal,
`FF_FS_REENTRANT=0`) и `tft_app` (FreeRTOS, `FF_FS_REENTRANT=1`, `FF_VOLUMES=3`).
Общий только `port_fatfs_sd` — он не включает `ff.h` напрямую.

---

## Зависимости

```cmake
target_link_libraries(bsp_sd
    PUBLIC  bsp_status       # bsp_status_t
    PRIVATE bsp_sdmmc_config # BOARD_SD_Config, sdmmc_config.h, sdk_sdmmc_sd
)
```

`sdk_sdmmc_sd` — транзитивно через `bsp_sdmmc_config`.
`sdk_usdhc` — транзитивно через `sdk_sdmmc_sd`.

---

## Ограничения

- Модуль рассчитан на одну карту (USDHC1, `g_sd` — единственный дескриптор).
- `bsp_sd_is_inserted()` читает аппаратный регистр без дебаунса. При
  механическом детекте возможны ложные срабатывания в момент вставки/извлечения —
  добавляй дебаунс в вызывающем коде если нужно.
- Hot-swap не поддерживается: `bsp_sd_deinit()` + `bsp_sd_init()` между сессиями.
