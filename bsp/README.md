# BSP — Board Support Package

> Целевая платформа: NXP MIMXRT1052CVJ5B
> Используется в: `firmware/bootloader`, `firmware/test`, `firmware/tft_app`

---

## Концепция

BSP — единственное место в монорепо где есть знание о конкретном железе. Все три прошивки работают с периферией только через BSP. Прямых вызовов NXP SDK (`fsl_*.h`) за пределами `bsp/` быть не должно.

```bash
firmware/test        firmware/bootloader        firmware/tft_app
      ↓                      ↓                         ↓
  ┌─────────────────────────────────────────────────────┐
  │                        BSP                          │
  │   bsp_led  bsp_opto  bsp_tick  bsp_uart_host  ...  │
  └─────────────────────────────────────────────────────┘
      ↓                      ↓                         ↓
  ┌─────────────────────────────────────────────────────┐
  │                  NXP SDK / middleware                │
  │       fsl_lpuart   fsl_gpio   fsl_iomuxc   ...      │
  └─────────────────────────────────────────────────────┘
```

---

## Структура

```bash
bsp/
├── CMakeLists.txt          # корневой: bsp_board + add_subdirectory для компонентов
├── README.md               # этот файл
│
├── generated/              # ← MCUXpresso Config Tools, не редактировать руками
│   ├── board.c / board.h
│   ├── clock_config.c / clock_config.h
│   ├── pin_mux.c / pin_mux.h
│   ├── syscalls.c
│   ├── TFT_Board.mex       # ← источник истины, открывать в Config Tools
│   └── startup/
│       └── startup_MIMXRT1052.S
│
├── common/                 # bsp_status_t и общие типы
├── led/                    # bsp_led     — два UserLed (GPIO3[3], GPIO3[4])
├── tick/                   # bsp_tick    — SysTick / FreeRTOS-совместимый таймер
├── uart_host/              # bsp_uart_host — LPUART1 (MCU-Link VCOM, J2)
│   └── mocks/              # fff-заглушки для host-тестов
├── opto/                   # bsp_opto    — оптоизолированные входы PS2801-4
└── usb_cdc/                # bsp_usb_cdc — USB CDC ACM
```

---

## Компоненты CMake

Каждый компонент — отдельная статическая библиотека `bsp_<name>`.

### bsp_board — фундамент

```cmake
target_link_libraries(bsp_<любой_компонент> PUBLIC bsp_board)
```

Содержит стартап, clock config, pin mux, board init. Формируется из `generated/`
и не должен меняться руками — только через MCUXpresso Config Tools.

### Boot-стратегии — INTERFACE-библиотеки

| Таргет CMake | Сценарий | Кто использует |
|---|---|---|
| `bsp_boot_xip` | XIP — исполнение из Flash | `firmware/test`, `firmware/tft_app` |
| `bsp_boot_ram` | исполнение из ITCM/DTCM | HIL target-прошивки (`tests/target/`) |

Подключается явно в каждом проекте:

```cmake
target_link_libraries(firmware_test PRIVATE bsp_board bsp_boot_xip ...)
target_link_libraries(test_hil_opto PRIVATE bsp_board bsp_boot_ram ...)
```

### Компоненты периферии

| Библиотека | Модуль | README |
|---|---|---|
| `bsp_led` | `led/` | [led/README.md](led/README.md) |
| `bsp_tick` | `tick/` | [tick/README.md](tick/README.md) |
| `bsp_uart_host` | `uart_host/` | [uart_host/README.md](uart_host/README.md) |
| `bsp_opto` | `opto/` | [opto/README.md](opto/README.md) |
| `bsp_usb_cdc` | `usb_cdc/` | [usb_cdc/README.md](usb_cdc/README.md) |

---

## Правила написания компонентов

### Граница изоляции

Публичные заголовки (`include/bsp/*.h`) не должны содержать ни одного `#include` из NXP SDK. Снаружи BSP — только стандартные типы C и собственные типы проекта.

```c
/* ПРАВИЛЬНО — bsp/opto/include/bsp/opto.h */
#include <stdint.h>
#include <stdbool.h>
#include "bsp/status.h"

/* НЕПРАВИЛЬНО */
#include "fsl_gpio.h"   /* ← утечка NXP SDK наружу */
```

Платформенные хедеры (`fsl_*.h`) живут только в `src/` — как PRIVATE зависимости.

### Структура одного компонента

```bash
bsp/<name>/
├── CMakeLists.txt
├── README.md
├── include/
│   └── bsp/
│       └── <name>.h        # публичный API — без NXP хедеров
└── src/
    └── <name>.c            # реализация — fsl_*.h только здесь
```

```cmake
# bsp/<name>/CMakeLists.txt — шаблон
add_library(bsp_<name> STATIC src/<name>.c)

target_include_directories(bsp_<name>
    PUBLIC  include/
    PRIVATE src/
)

target_link_libraries(bsp_<name>
    PUBLIC  bsp_status
    PRIVATE bsp_board sdk_<driver>
)
```

### Защита от host-сборки

Компоненты с зависимостью от железа закрываются guard-ом в CMakeLists:

```cmake
if(BUILD_TESTS_HOST)
  return()
endif()
```

Компоненты которые тестируются на хосте предоставляют fff-заглушки
в `mocks/` (пример — `uart_host/mocks/`).

---

## generated/ — MCUXpresso Config Tools

`generated/` — выхлоп Config Tools. Содержит конфигурацию тактирования,
пинов и периферии для конкретной платы. Источник истины — `TFT_Board.mex`.

**Трогать нельзя:** редактировать файлы из `generated/` руками — изменения
потеряются при следующей перегенерации.

**Как добавить новый пин или периферию:** открыть `TFT_Board.mex` в
MCUXpresso Config Tools → внести изменения → Update Code → закоммитить
изменённые файлы из `generated/`.

---

## Добавление нового компонента — чеклист

```bash
[ ] bsp/<name>/include/bsp/<name>.h  — публичный API без NXP хедеров
[ ] bsp/<name>/src/<name>.c          — реализация
[ ] bsp/<name>/CMakeLists.txt        — guard BUILD_TESTS_HOST + зависимости
[ ] bsp/<name>/README.md             — аппаратура + API + использование
[ ] bsp/CMakeLists.txt               — add_subdirectory(<name>)
[ ] firmware/*/CMakeLists.txt        — добавить bsp_<name> в нужные прошивки
```
