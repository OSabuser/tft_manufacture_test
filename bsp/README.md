# BSP — Board Support Package

> Целевая платформа: NXP IMXRT1052CVJ5B  
> Используется в: `firmware/bootloader`, `firmware/test`, `firmware/tft_app`

---

## Концепция

BSP — единственное место в монорепо где есть знание о конкретном железе. Все три прошивки работают с периферией только через BSP. Прямых вызовов NXP SDK (`fsl_*.h`) за пределами `bsp/` быть не должно.

```bash
firmware/test        firmware/bootloader        firmware/tft_app
      ↓                      ↓                         ↓
  ┌─────────────────────────────────────────────────────┐
  │                        BSP                          │
  │   bsp_usb_cdc  bsp_uart  bsp_can  bsp_sdram  ...   │
  └─────────────────────────────────────────────────────┘
      ↓                      ↓                         ↓
  ┌─────────────────────────────────────────────────────┐
  │                  NXP SDK / middleware                │
  │      fsl_lpuart   fsl_flexcan   usb stack  ...      │
  └─────────────────────────────────────────────────────┘
```

---

## Структура

```bash
bsp/
├── CMakeLists.txt          # корневой: add_subdirectory для всех компонентов
├── README.md               # этот файл
│
├── generated/              # ← MCUXpresso Config Tools, не редактировать руками
│   ├── board.c / board.h
│   ├── clock_config.c / clock_config.h
│   ├── pin_mux.c / pin_mux.h
│   ├── peripherals.c / peripherals.h
│   └── startup/
│       └── startup_MIMXRT1052.S
│
├── usb_cdc/                # USB CDC ACM (Virtual COM Port)
├── uart/                   # LPUART: TTL + изолированный RX +24V
├── can/                    # FlexCAN
├── sdram/                  # SEMC → SDRAM 32 MB (MT48LC16M16)
├── qspi/                   # FlexSPI → W25Q128 (QSPI Flash)
├── sdio/                   # uSDHC → uSD слот
├── display/                # eLCDIF → RGB888
├── gpio/                   # кнопки, LED, гальванически развязанные входы
└── mqs/                    # MQS → аналоговый аудио выход
```

---

## Компоненты CMake

Каждый компонент — отдельная статическая библиотека `bsp_<name>`.

### bsp_board — фундамент, от которого зависят все остальные

```cmake
target_link_libraries(bsp_<любой_компонент> PUBLIC bsp_board)
```

`bsp_board` содержит: стартап, clock config, pin mux, board init. Формируется из `generated/` и не должен меняться руками — только через MCUXpresso Config Tools с последующей перегенерацией.

### Boot-сценарии — INTERFACE-библиотеки

Каждая прошивка выбирает один сценарий исполнения кода:

| Таргет CMake | Сценарий | Кто использует |
|---|---|---|
| `bsp_boot_xip` | XIP — код исполняется из Flash | `firmware/test`, `firmware/tft_app` |
| `bsp_boot_itcm` | копирование в ITCM | `firmware/bootloader` |
| `bsp_boot_sdram` | копирование в SDRAM | зарезервировано |

Подключается явно в каждом проекте:

```cmake
target_link_libraries(firmware_test PRIVATE bsp_board bsp_boot_xip ...)
```

### Компоненты периферии

Каждый компонент подключается независимо — прошивка линкует только то что использует:

```cmake
# firmware/test — использует всё
target_link_libraries(firmware_test PRIVATE
    bsp_board bsp_boot_xip
    bsp_usb_cdc bsp_uart bsp_can
    bsp_sdram bsp_qspi bsp_sdio
    bsp_rtc bsp_display bsp_gpio bsp_ir
)

# firmware/bootloader — минимальный набор
target_link_libraries(bootloader PRIVATE
    bsp_board bsp_boot_itcm
    bsp_usb_cdc bsp_qspi bsp_uart
)
```

---

## Правила написания компонентов

### Граница изоляции

Публичные заголовки компонента (`include/bsp/*.h`) не должны содержать ни одного `#include` из NXP SDK. Снаружи BSP — только стандартные типы C (`stdint.h`, `stdbool.h`, `stddef.h`) и собственные типы проекта.

```c
/* ПРАВИЛЬНО — bsp/usb_cdc/include/bsp/usb_cdc.h */
#include <stdint.h>
#include <stdbool.h>
typedef enum { USB_CDC_OK, USB_CDC_ERR_NOT_READY } usb_cdc_status_t;
usb_cdc_status_t usb_cdc_init(void);

/* НЕПРАВИЛЬНО */
#include "fsl_common.h"   /* ← утечка NXP SDK наружу */
```

Платформенные хедеры (`fsl_*.h`, `usb_device_*.h`) живут только в `src/` — как PRIVATE зависимости.

### Структура одного компонента

```bash
bsp/<name>/
├── CMakeLists.txt
├── include/
│   └── bsp/
│       └── <name>.h        # публичный API — без NXP хедеров
└── src/
    ├── <name>.c             # реализация
    └── <конфиг>.h           # приватные конфиги стека (напр. usb_device_config.h)
```

```cmake
# bsp/<name>/CMakeLists.txt — шаблон
add_library(bsp_<name> STATIC src/<name>.c)

target_include_directories(bsp_<name>
    PUBLIC  include/         # bsp/<name>.h доступен снаружи
    PRIVATE src/             # конфиги и NXP хедеры — только внутри
)

target_link_libraries(bsp_<name>
    PUBLIC  bsp_board        # транзитивно во все потребители
    PRIVATE sdk_<driver>     # NXP SDK — не торчит наружу
)
```

### Защита от host-сборки

Каждый компонент должен быть безопасен при `BUILD_TESTS_HOST=ON`. Вариантов два:

**А — guard в CMakeLists (рекомендуется для большинства компонентов):**

```cmake
if(BUILD_TESTS_HOST)
  return()
endif()
```

**Б — stub-реализация для компонентов которые тестируются на хосте:**

```c
/* src/usb_cdc.c */
#ifdef BSP_USB_CDC_VIRTUAL
/* заглушка — пишет в stdout, используется в host-тестах */
usb_cdc_status_t usb_cdc_write(const uint8_t *data, size_t len) {
    fwrite(data, 1, len, stdout);
    return USB_CDC_OK;
}
#else
/* реальная реализация через NXP USB stack */
#endif
```

---

## Связь с generated/

`generated/` — выхлоп MCUXpresso Config Tools. Содержит конфигурацию тактирования, пинов и периферии для конкретной платы.

**Что трогать можно:** файлы в `generated/` можно и нужно перегенерировать через Config Tools при изменении схемы.

**Что трогать нельзя:** редактировать `generated/` руками — изменения потеряются при следующей перегенерации.

**Как добавить новый пин или периферию:** открыть проект в MCUXpresso Config Tools → внести изменения → Update Code → закоммитить изменённые файлы из `generated/`.

---

## Добавление нового компонента — чеклист

```bash
[ ] Создать bsp/<name>/ со структурой include/src/CMakeLists.txt
[ ] Публичный хедер include/bsp/<name>.h — без NXP хедеров
[ ] target_link_libraries: PUBLIC bsp_board, PRIVATE sdk_*
[ ] Guard BUILD_TESTS_HOST в CMakeLists или stub-реализация в .c
[ ] add_subdirectory(bsp/<name>) в bsp/CMakeLists.txt
[ ] Добавить target в нужные прошивки (firmware/*/CMakeLists.txt)
```
