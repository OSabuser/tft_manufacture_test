# bsp_display — ELCDIF RGB-дисплей (TFT7 / TFT8 / TFT4 / TFT10)

Инициализация ELCDIF в RGB-режиме, настройка пиксельного клока, управление
GPIO подсветки и пинами ориентации/режима. Предоставляет API для смены
фреймбуфера, поворота и нотификации о завершении кадра через ISR-safe callback.
Bare-metal совместим, без FreeRTOS.

---

## Аппаратура

**Управляющие GPIO:**

| Сигнал       | Пин MCU       | Корпус | GPIO      | Назначение                   |
| ------------ | ------------- | ------ | --------- | ---------------------------- |
| LcdLed       | GPIO_AD_B1_04 | L12    | GPIO1[20] | Подсветка, active-high       |
| LcdLR (SHLR) | GPIO_AD_B1_12 | H12    | GPIO1[28] | Горизонтальное направление   |
| LcdMode      | GPIO_AD_B1_13 | H11    | GPIO1[29] | HIGH = DE mode (обязательно) |
| LcdUD (UPDN) | GPIO_AD_B1_14 | G12    | GPIO1[30] | Вертикальное направление     |
| LcdDithb     | GPIO_AD_B1_15 | J14    | GPIO1[31] | HIGH = dithering disable     |

**RGB-интерфейс ELCDIF:**

| Сигнал       | Пины MCU         | Примечание                      |
| ------------ | ---------------- | ------------------------------- |
| LCDIF_CLK    | GPIO_B0_00 (D7)  |                                 |
| LCDIF_ENABLE | GPIO_B0_01 (E7)  |                                 |
| LCDIF_HSYNC  | GPIO_B0_02 (E8)  |                                 |
| LCDIF_VSYNC  | GPIO_B0_03 (D8)  |                                 |
| DATA[0–7]    | GPIO_B0_04–B0_11 | C8, B8, A8, A9, B9, C9, D9, A10 |
| DATA[8–15]   | GPIO_B0_12–B1_03 |                                 |
| DATA[16–23]  | GPIO_B1_04–B1_11 |                                 |

Пины настроены в `BOARD_InitPins()`. `LCDIF_IRQHandler` размещён в ITCM
(`AT_QUICKACCESS_SECTION_CODE`), приоритет `DISPLAY_IRQ_PRIORITY = 2`.

**Пиксельный клок (PLL2 = 528 МГц):**

| Дисплей | clk_mux   | pre_div | div | Частота                             |
| ------- | --------- | ------- | --- | ----------------------------------- |
| TFT7    | PLL2      | 2       | 4   | 528 / 3 / 5 = **35.2 МГц**          |
| TFT8    | PLL2      | 2       | 3   | 528 / 3 / 4 = **44.0 МГц**          |
| TFT4    | Video PLL | —       | —   | требует `CLOCK_InitVideoPll` (TODO) |
| TFT10   | —         | —       | —   | зарезервировано                     |

---

## API

```c
bsp_status_t bsp_display_init(bsp_display_type_t type,
                              uint32_t framebuffer_addr,
                              bsp_display_frame_cb_t p_on_frame_done);

/* То же + явный формат пикселя framebuffer'а (bsp_display_init — обёртка
 * с BSP_DISPLAY_PIXEL_XRGB8888, совместимость со старыми потребителями). */
bsp_status_t bsp_display_init_ex(bsp_display_type_t type,
                                 uint32_t framebuffer_addr,
                                 bsp_display_frame_cb_t p_on_frame_done,
                                 bsp_display_pixel_format_t format);

bsp_status_t bsp_display_deinit(void);
bsp_status_t bsp_display_set_rotation(bsp_display_rotation_t rotation);
void         bsp_display_set_next_buffer(uint32_t framebuffer_addr);

const bsp_display_size_t *bsp_display_get_size(void);
bsp_display_type_t        bsp_display_get_type(void);
```

**Формат пикселя framebuffer'а** (что ELCDIF читает из памяти):

```c
typedef enum {
    BSP_DISPLAY_PIXEL_XRGB8888 = 0U, /* 32 бита/пиксель — по умолчанию      */
    BSP_DISPLAY_PIXEL_RGB565,        /* 16 бит/пиксель — ½ полосы сканаута */
} bsp_display_pixel_format_t;
```

Формат задаёт ТОЛЬКО ширину слова в памяти (`LCDIF CTRL.WORD_LENGTH`) — то
есть нагрузку непрерывного DMA-сканаута на SDRAM. Ширина шины пинов панели —
всегда 24 бита и от формата не зависит: для RGB565 ELCDIF расширяет 565→24
на пинах (проверено на TFT8: белый — чистый белый, чистые R/G/B корректны).
Оба режима сосуществуют: старый потребитель (`firmware_test/test_display.c`)
работает через `bsp_display_init()` (XRGB8888), `tft_app` (`services/gfx`,
гибрид bpp) — через `bsp_display_init_ex(..., BSP_DISPLAY_PIXEL_RGB565)`.

Практика по полосе (TFT8 800×600 @ ~65 Гц, SDRAM 16 бит @ ~136 МГц ≈ 272 МБ/с):
сканаут XRGB8888 ≈ 126 МБ/с (46% всей полосы), RGB565 ≈ 63 МБ/с — вдвое
меньше давления на SDRAM для всех остальных мастеров (CPU/PXP).

**Типы дисплея:**

```c
typedef enum {
    BSP_DISPLAY_TFT4  = 0U,  /* 480 × 272  */
    BSP_DISPLAY_TFT7,         /* 1024 × 600 */
    BSP_DISPLAY_TFT8,         /* 800 × 600  */
    BSP_DISPLAY_TFT10,        /* зарезервировано */
    BSP_DISPLAY_COUNT,
} bsp_display_type_t;
```

**Ротация → LR/UD (TFT7/TFT8):**

```c
typedef enum {
    BSP_DISPLAY_ROTATE_0         = 0U,  /* LR=1, UD=0 */
    BSP_DISPLAY_FLIP_VERTICAL,           /* LR=1, UD=1 */
    BSP_DISPLAY_FLIP_BOTH,               /* LR=0, UD=1 */
    BSP_DISPLAY_FLIP_HORIZONTAL,         /* LR=0, UD=0 */
} bsp_display_rotation_t;
```

**Коды возврата `bsp_display_init()` / `bsp_display_init_ex()`:**

| Код                     | Условие                        |
| ----------------------- | ------------------------------ |
| `BSP_OK`                | Инициализирован (или уже был)  |
| `BSP_ERR_PARAM`         | `type >= BSP_DISPLAY_COUNT`    |
| `BSP_ERR_NOT_SUPPORTED` | TFT4 — Video PLL не реализован |

Обе init-функции идемпотентны: повторный вызов без `deinit` возвращает
`BSP_OK` без побочных эффектов. Callback `p_on_frame_done` должен быть
ISR-safe (`NULL` допускается).

`bsp_display_set_next_buffer()` безопасна из ISR и из задачи; переключение
происходит аппаратно по окончании текущего кадра.

---

## Быстрый старт

```c
#include "bsp/display.h"

/* Фреймбуфер — в NonCacheable SDRAM, выровнен по 64 байтам */
static AT_NONCACHEABLE_SECTION_ALIGN(
    uint32_t fb[BSP_DISPLAY_MAX_HEIGHT * BSP_DISPLAY_MAX_WIDTH], 64U);

static volatile bool g_frame_done;
static void on_frame_done(void) { g_frame_done = true; }

/* После board_hw_init(): */
bsp_display_init(BSP_DISPLAY_TFT8, (uint32_t)fb, on_frame_done);
bsp_display_set_rotation(BSP_DISPLAY_ROTATE_0);

/* Показать кадр */
g_frame_done = false;
bsp_display_set_next_buffer((uint32_t)fb);
while (!g_frame_done) {}
```

---

## CMake

```cmake
target_link_libraries(firmware_test PRIVATE bsp_display)

target_compile_definitions(firmware_test PRIVATE
    DISPLAY_TEST_TYPE=BSP_DISPLAY_TFT8
)
```

`DISPLAY_TEST_TYPE` — параметр тест-модуля `test_display.c`, не самого BSP.

**Зависимости модуля:**

| Зависимость  | Тип     | Описание                                    |
| ------------ | ------- | ------------------------------------------- |
| `bsp_status` | PUBLIC  | `bsp_status_t` в публичном API              |
| `bsp_board`  | PRIVATE | Транзитивно: `BOARD_*`, IOMUXC              |
| `sdk_elcdif` | PRIVATE | `fsl_elcdif.h`, `fsl_clock.h`, `fsl_gpio.h` |
