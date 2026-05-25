# bsp_display — ELCDIF RGB-дисплей (TFT4 / TFT7 / TFT8 / TFT10)

> Расположение: `bsp/display/`
> Публичный заголовок: `bsp/display/include/bsp/display.h`
> Реализация: `bsp/display/src/display.c`

Модуль инициализирует ELCDIF в RGB-режиме, настраивает пиксельный клок,
управляет GPIO подсветки и пинами ориентации/режима LR, UD, MODE, DITHB.
Предоставляет минимальное API для смены фреймбуфера, поворота и оповещения
о завершении кадра через callback. Bare-metal совместим (без FreeRTOS).

---

## Аппаратный контекст

| Сигнал / параметр | Аппаратное назначение                                                                            |
| ----------------- | ------------------------------------------------------------------------------------------------ |
| ELCDIF            | NXP ELCDIF, RGB-режим, формат пикселя `kELCDIF_PixelFormatXRGB8888`, шина `kELCDIF_DataBus24Bit` |
| Подсветка         | `GPIO1[20]` — active-high                                                                        |
| LR (горизонт.)    | `GPIO1[28]` (`Lcdlr_value`)                                                                      |
| MODE              | `GPIO1[29]` — HIGH = DE mode (обязательно для ELCDIF)                                            |
| UD (вертикаль)    | `GPIO1[30]`                                                                                      |
| DITHB             | `GPIO1[31]` — HIGH = dithering disable (IC default)                                              |
| Пиксельный клок   | PLL2 (`mux=0`) для TFT7/TFT8; Video PLL (`mux=2`) для TFT4                                       |
| IRQ               | `LCDIF_IRQHandler` в ITCM, приоритет `DISPLAY_IRQ_PRIORITY = 2`                                  |

`IOMUXC` конфигурируется в `BOARD_InitPins()` за пределами модуля — здесь
выполняется только `GPIO_PinWrite`.

Делители пиксельного клока (исходный код, PLL2 = 528 МГц):

| Дисплей | clk_mux   | pre_div  | div | Эффективная частота            |
| ------- | --------- | -------- | --- | ------------------------------ |
| TFT7    | PLL2      | 2        | 4   | `528/3/5 = 35.2 МГц`           |
| TFT8    | PLL2      | 2        | 3   | `528/3/4 = 44.0 МГц`           |
| TFT4    | Video PLL | — (TODO) | —   | требует `CLOCK_InitVideoPll`   |
| TFT10   | —         | —        | —   | таблица не заполнена (`{ 0 }`) |

Тайминги HSW/HFP/HBP/VSW/VFP/VBP заданы константами в `display.c`
(`DISPLAY_TFT7_*`, `DISPLAY_TFT8_*`, `DISPLAY_TFT4_*`).

---

## Состав модуля

```bash
bsp/display/
├── include/bsp/display.h    # публичный заголовок
├── src/display.c            # реализация API + LCDIF_IRQHandler
└── CMakeLists.txt           # цель bsp_display
```

`LCDIF_IRQHandler` размещён в ITCM (`AT_QUICKACCESS_SECTION_CODE`) и только
вызывает зарегистрированный callback; состояние модуля он не модифицирует.

---

## Публичные типы

```c
typedef enum bsp_display_type_e {
    BSP_DISPLAY_TFT4  = 0U, /* 480 × 272, нет ножек ориентации/MODE/DITHB */
    BSP_DISPLAY_TFT7,       /* 1024 × 600, LR + UD + MODE + DITHB         */
    BSP_DISPLAY_TFT8,       /* 800  × 600, LR + UD + MODE + DITHB         */
    BSP_DISPLAY_TFT10,      /* зарезервировано, спецификации уточняются   */
    BSP_DISPLAY_COUNT,
} bsp_display_type_t;

typedef enum bsp_display_rotation_e {
    BSP_DISPLAY_ROTATE_0   = 0U, /* LR=1 UD=0 */
    BSP_DISPLAY_ROTATE_90,       /* LR=1 UD=1 */
    BSP_DISPLAY_ROTATE_180,      /* LR=0 UD=1 */
    BSP_DISPLAY_ROTATE_270,      /* LR=0 UD=0 */
} bsp_display_rotation_t;

typedef struct bsp_display_size_s {
    uint16_t width;
    uint16_t height;
} bsp_display_size_t;

typedef void (*bsp_display_frame_cb_t)(void);  /* ISR-safe */
```

Размеры максимального дисплея (для статического выделения буферов):

```c
#define BSP_DISPLAY_MAX_WIDTH  1024U
#define BSP_DISPLAY_MAX_HEIGHT 600U
```

---

## Публичный API

```c
bsp_status_t bsp_display_init(bsp_display_type_t type,
                              uint32_t framebuffer_addr,
                              bsp_display_frame_cb_t p_on_frame_done);

bsp_status_t bsp_display_deinit(void);

bsp_status_t bsp_display_set_rotation(bsp_display_rotation_t rotation);

void bsp_display_set_next_buffer(uint32_t framebuffer_addr);

const bsp_display_size_t *bsp_display_get_size(void);

bsp_display_type_t bsp_display_get_type(void);
```

### `bsp_display_init`

Настраивает пиксельный клок (через `CLOCK_SetMux` / `CLOCK_SetDiv`), включает
`kCLOCK_LcdPixel`, поднимает подсветку, для TFT7/TFT8 инициализирует ножки
ориентации (`ROTATE_0`: LR=1, UD=0) и режима (MODE=1: DE-mode, DITHB=1),
конфигурирует ELCDIF через `ELCDIF_RgbModeInit` и запускает его через
`ELCDIF_RgbModeStart`. Включает прерывание `kELCDIF_CurFrameDoneInterruptEnable`
с приоритетом `DISPLAY_IRQ_PRIORITY = 2`.

Требования к аргументам и поведение:

- `framebuffer_addr` — физический адрес первого фреймбуфера. Согласно
  заголовку, ожидается выравнивание по 64 байтам и размещение в NonCacheable
  SDRAM. (Само значение модуль не проверяет — это контракт потребителя.)
- `p_on_frame_done` — ISR-safe callback; `NULL` означает «без callback».
  Сохраняется до включения IRQ, чтобы избежать гонки.
- Повторный вызов без `bsp_display_deinit()` — идемпотентен, возвращает
  `BSP_OK` без побочных эффектов.

Коды возврата:

| Код                     | Когда                                                                                                     |
| ----------------------- | --------------------------------------------------------------------------------------------------------- |
| `BSP_OK`                | Дисплей инициализирован (или уже был инициализирован).                                                    |
| `BSP_ERR_PARAM`         | `type >= BSP_DISPLAY_COUNT`.                                                                              |
| `BSP_ERR_NOT_SUPPORTED` | `type` требует Video PLL (TFT4) — `init_pixelclock` возвращает ошибку до реализации `CLOCK_InitVideoPll`. |

> Поведение для `BSP_DISPLAY_TFT10` целостно не описано в коде: запись в
> `K_HW_CFG[BSP_DISPLAY_TFT10]` сделана как `{ 0 }`. Использовать TFT10 как
> рабочий параметр сейчас не гарантируется — типу зарезервировано место в
> enum.

### `bsp_display_deinit`

Останавливает ELCDIF (`ELCDIF_RgbModeStop`), выключает IRQ, вызывает
`ELCDIF_Deinit`, отключает `kCLOCK_LcdPixel` и гасит подсветку. Безопасен
при вызове до `init` и повторно. Всегда возвращает `BSP_OK`.

### `bsp_display_set_rotation`

Переключает ориентацию через ножки LR/UD для дисплеев с
`has_orientation_pins = true` (TFT7/TFT8). Для TFT4 поддерживается только
`BSP_DISPLAY_ROTATE_0`; другие значения возвращают `BSP_ERR_NOT_SUPPORTED`.
До `bsp_display_init()` возвращает `BSP_ERR_INIT`. Неизвестная rotation —
`BSP_ERR_PARAM`.

Соответствие rotation → LR/UD (из `display.c`):

| Rotation                      | LR  | UD  |
| ----------------------------- | --- | --- |
| `BSP_DISPLAY_ROTATE_0`        | 1   | 0   |
| `BSP_DISPLAY_FLIP_VERTICAL`   | 1   | 1   |
| `BSP_DISPLAY_FLIP_BOTH`       | 0   | 1   |
| `BSP_DISPLAY_FLIP_HORIZONTAL` | 0   | 0   |

### `bsp_display_set_next_buffer`

Тонкая обёртка над `ELCDIF_SetNextBufferAddr`. Согласно заголовку, безопасна
из ISR и из задачи; переключение произойдёт аппаратно по окончании текущего
кадра. Возврата нет.

### `bsp_display_get_size` / `bsp_display_get_type`

Возвращают зафиксированные при `init` параметры активного дисплея.
`bsp_display_get_size()` возвращает `NULL` до `init`; `bsp_display_get_type()`
возвращает `BSP_DISPLAY_COUNT`, если дисплей не инициализирован.

### Callback `bsp_display_frame_cb_t`

Вызывается из `LCDIF_IRQHandler` при флаге `kELCDIF_CurFrameDone`. Должен
быть ISR-safe: запись в `volatile`, `xSemaphoreGiveFromISR()` и т.п.; любые
блокирующие операции запрещены (требование из заголовка).

---

## Порядок использования

```c
#include "bsp/display.h"

/* Фреймбуфер — статический, в NonCacheable SDRAM, выровнен по 64 байтам. */
static AT_NONCACHEABLE_SECTION_ALIGN(
    uint32_t fb[BSP_DISPLAY_MAX_HEIGHT * BSP_DISPLAY_MAX_WIDTH], 64U);

static volatile bool g_frame_done;
static void on_frame_done(void) { g_frame_done = true; }  /* ISR-safe */

void app_init(void)
{
    /* board_hw_init() / CLOCK_*/

    bsp_status_t s = bsp_display_init(BSP_DISPLAY_TFT8,
                                      (uint32_t) fb,
                                      on_frame_done);
    if (s != BSP_OK) { /* обработать */ }

    (void) bsp_display_set_rotation(BSP_DISPLAY_ROTATE_0);

    /* Залить буфер и показать кадр */
    g_frame_done = false;
    bsp_display_set_next_buffer((uint32_t) fb);
    while (!g_frame_done) { /* ждать */ }
}
```

---

## Зависимости и CMake

```cmake
# bsp/display/CMakeLists.txt
add_library(bsp_display STATIC src/display.c)

target_include_directories(bsp_display
    PUBLIC  include/
    PRIVATE src/)

target_link_libraries(bsp_display
    PUBLIC  bsp_status
    PRIVATE bsp_board sdk_elcdif)
```

- `bsp_status` (PUBLIC) — `bsp_status_t` в публичном API.
- `bsp_board` (PRIVATE) — общие board-уровневые символы (`BOARD_*`, IOMUXC).
- `sdk_elcdif` (PRIVATE) — `fsl_elcdif.h`, тип `elcdif_rgb_mode_config_t`,
  функции `ELCDIF_*`, флаги полярности.

Дополнительно `display.c` подключает `fsl_clock.h` (`CLOCK_SetMux`,
`CLOCK_SetDiv`, `CLOCK_EnableClock`, `kCLOCK_LcdifPreMux/PreDiv/Div`,
`kCLOCK_LcdPixel`) и `fsl_gpio.h` (`GPIO_PinWrite`) — символы предоставляются
SDK через транзитивные зависимости.

Цель не собирается при `BUILD_TESTS_HOST=ON` (host-сборка) — ранний
`return()` в `CMakeLists.txt`.

Потребитель (`firmware/test/CMakeLists.txt`):

```cmake
target_link_libraries(firmware_test PRIVATE
    ...
    bsp_display
    ...
)

target_compile_definitions(firmware_test PRIVATE
    DISPLAY_TEST_TYPE=BSP_DISPLAY_TFT8
    ...
)
```

`DISPLAY_TEST_TYPE` — параметр **тест-модуля** (`test_display.c`), а не самого
`bsp_display`; см. ниже.

---

## Связь с firmware_test (`test_display.c`)

Тест-модуль `firmware/test/src/tests/test_display.c` использует это BSP так:

- Тип дисплея определяется макросом `DISPLAY_TEST_TYPE` (по умолчанию
  `BSP_DISPLAY_TFT8`), задаётся через `target_compile_definitions` в
  `firmware/test/CMakeLists.txt`.
- В `init` тест-модуль вызывает
  `bsp_display_init((bsp_display_type_t) DISPLAY_TEST_TYPE, (uint32_t) g_s_framebuf, display_frame_cb)`,
  затем `bsp_display_set_rotation(BSP_DISPLAY_ROTATE_0)`.
- Фреймбуфер — `g_s_framebuf[BSP_DISPLAY_MAX_HEIGHT * BSP_DISPLAY_MAX_WIDTH]`
  типа `uint32_t`, выровнен по 64 байтам через `AT_NONCACHEABLE_SECTION_ALIGN`.
- Frame sync — `volatile bool g_s_frame_done`, выставляется в callback
  `display_frame_cb` (записывает `true`); сбрасывается перед каждым
  `bsp_display_set_next_buffer((uint32_t) g_s_framebuf)`.
- Два этапа теста (фактическое поведение из кода):
  - **Этап 1 — Цвет**: четыре шага `step_color(RED, GREEN, BLUE, WHITE)`
    с XRGB8888 цветами (`0x00FF0000`, `0x0000FF00`, `0x000000FF`, `0x00FFFFFF`).
    Каждый шаг — заливка → ожидание кадра → confirm оператора
    с таймаутом `DISPLAY_CONFIRM_TIMEOUT_MS = 15000 мс`.
  - **Этап 2 — Ротация**: выполняется только если
    `bsp_display_get_type() != BSP_DISPLAY_TFT4`. Заливка «левая половина RED,
    правая BLUE», затем confirm для `ROTATE_0`, затем `ROTATE_90` через
    `bsp_display_set_rotation()`, после чего восстанавливается `ROTATE_0`.
- В `deinit` вызывается `bsp_display_deinit()`.

Дескриптор тест-модуля:

```c
const test_module_t K_TEST_DISPLAY = {
    .id           = "display",
    .name         = "TFT Display RGB888",
    .critical     = false,
    .requires_hil = false,
    ...
};
```

---

## Ограничения и замечания

- **TFT4 не поддерживается** до реализации Video PLL: `bsp_display_init`
  возвращает `BSP_ERR_NOT_SUPPORTED`, в исходнике это явно отмечено как TODO
  (`CLOCK_InitVideoPll`). Соответственно, и тест-модуль `display` пропускает
  этап ротации для TFT4 и поддерживает только `ROTATE_0` через API.
- **TFT10** присутствует только как зарезервированное значение `enum`
  (`K_HW_CFG[BSP_DISPLAY_TFT10] = { 0 }`). Реальные тайминги/делители не
  заполнены; конкретное поведение `bsp_display_init(BSP_DISPLAY_TFT10, ...)`
  не описано документацией модуля и не гарантируется.
- `bsp_display_init` идемпотентен: повторный вызов без `deinit` возвращает
  `BSP_OK` и не перенастраивает аппаратуру. Чтобы переинициализировать с
  другим типом или новым адресом фреймбуфера, нужно сначала вызвать
  `bsp_display_deinit()`.
- Адрес фреймбуфера должен указывать на NonCacheable память (согласно
  заголовку); модуль не делает cache maintenance над буфером.
- `bsp_display_set_next_buffer` возвращает `void` — отсутствие ошибки от
  ELCDIF предполагается; верификация переключения буфера — задача
  потребителя (например, через FRAME_DONE callback).
- IOMUXC ножек LR/UD/MODE/DITHB и пина подсветки конфигурируется в
  `BOARD_InitPins()` за пределами модуля; неправильная конфигурация
  IOMUXC проявится отсутствием реакции дисплея, а не возвратом ошибки из
  API.
