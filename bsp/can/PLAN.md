# План разработки bsp_can

## Обзор

Единый BSP-модуль `bsp/can/` для работы с FlexCAN2 (CAN1) на MIMXRT1052.
Одна шина, один трансивер SN65HVD230D. Модуль не зависит от FreeRTOS —
интеграция с RTOS делается через callback на стороне приложения.

**Приоритеты первой итерации (в порядке реализации):**

1. Базовый TX/RX (send + polling receive)
2. Фильтрация по ID (STD + EXT)
3. HIL-тесты (M5Stack + MicroPython CAN)
4. Host-тесты (fff mocks)
5. Callback-механизм для FreeRTOS (вторая итерация)

---

## Архитектура: один модуль — два паттерна использования

```
bsp/can/                          ← единственный модуль, без FreeRTOS
  include/bsp/can.h               ← публичный API
  src/bsp_can.c                   ← реализация поверх fsl_flexcan
  mocks/bsp_can_mock.h            ← fff-мок для host-тестов app-кода (будущее)

firmware/test/                    ← bare-metal: bsp_can_receive() polling
firmware/tft_app/                 ← FreeRTOS: callback → xQueueSendFromISR
```

Почему один модуль работает в обоих контекстах:

- `bsp_can_receive()` — блокирующий polling с таймаутом (bare-metal, HIL)
- `bsp_can_register_rx_callback()` — вызов из ISR, не блокируется (FreeRTOS)
- Вызывающий код выбирает один из двух механизмов, модуль не знает о контексте

---

## Привязка к железу

Из схемы (лист 3 + лист 5):

| Сигнал    | Пин MCU         | GPIO_AD_B0   | Функция      |
|-----------|-----------------|--------------|--------------|
| CAN_TX    | GPIO_AD_B0_14   | H14          | FLEXCAN2_TX  |
| CAN_RX    | GPIO_AD_B0_15   | L10          | FLEXCAN2_RX  |

Трансивер: SN65HVD230D (U9), CAN_H/CAN_L через RCLAMP0524PA (VD12).
Терминация: R30 120Ω + J1 (jumper для подключения).

> **Важно:** на RT1052 CAN1 в SDK = FlexCAN2 периферия (base `CAN2`).
> Нумерация в SDK сдвинута: `CAN1` base → не используется, `CAN2` base → наша шина.

---

## Этап 1 — Публичный API: `bsp/can/include/bsp/can.h`

```c
#pragma once

#include "bsp/common.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Константы ── */

#define BSP_CAN_DATA_MAX_LEN  8U
#define BSP_CAN_FILTER_MAX    16U   /* MB6..MB21 под RX-фильтры */

/* ── Типы ── */

typedef struct bsp_can_frame_s {
    uint32_t id;                         /* 11-bit STD или 29-bit EXT */
    uint8_t  dlc;                        /* 0..8 */
    bool     is_extended;                /* false = STD, true = EXT */
    bool     is_remote;                  /* RTR */
    uint8_t  data[BSP_CAN_DATA_MAX_LEN];
} bsp_can_frame_t;

typedef struct bsp_can_config_s {
    uint32_t bitrate;                    /* например 500000 */
} bsp_can_config_t;

/**
 * Callback из ISR-контекста.
 * Реализация НЕ ДОЛЖНА блокироваться.
 * Типичное использование: xQueueSendFromISR().
 */
typedef void (*bsp_can_rx_callback_t)(const bsp_can_frame_t *p_frame,
                                      void *p_user_ctx);

/* ── Init / Deinit ── */

bsp_status_t bsp_can_init(const bsp_can_config_t *p_config);
void         bsp_can_deinit(void);

/* ── Фильтрация ── */

/**
 * Настроить RX-фильтр на конкретный Message Buffer.
 *
 * @param index       0 .. BSP_CAN_FILTER_MAX-1
 * @param id          CAN ID для фильтрации
 * @param mask        битовая маска (1 = проверять, 0 = игнорировать)
 * @param is_extended true = 29-bit EXT, false = 11-bit STD
 */
bsp_status_t bsp_can_set_filter(uint8_t index,
                                uint32_t id,
                                uint32_t mask,
                                bool is_extended);

/** Принимать все фреймы (сброс всех фильтров). */
bsp_status_t bsp_can_accept_all(void);

/* ── TX ── */

/**
 * Отправить CAN-фрейм. Блокируется до завершения или таймаута.
 *
 * @param p_frame     фрейм для отправки
 * @param timeout_ms  таймаут в мс (0 = без ожидания)
 * @return BSP_OK, BSP_ERR_TIMEOUT, BSP_ERR_PARAM
 */
bsp_status_t bsp_can_send(const bsp_can_frame_t *p_frame,
                           uint32_t timeout_ms);

/* ── RX: polling ── */

/**
 * Принять CAN-фрейм (polling). Блокируется до приёма или таймаута.
 *
 * @param p_frame     буфер для принятого фрейма
 * @param timeout_ms  таймаут в мс (0 = проверить и вернуться)
 * @return BSP_OK, BSP_ERR_TIMEOUT
 */
bsp_status_t bsp_can_receive(bsp_can_frame_t *p_frame,
                              uint32_t timeout_ms);

/* ── RX: callback (для FreeRTOS bridge) ── */

/**
 * Зарегистрировать callback для приёма из ISR.
 * При регистрации callback, polling через bsp_can_receive() отключается.
 *
 * @param callback    функция-обработчик (NULL = отключить callback)
 * @param p_user_ctx  пользовательский контекст, передаётся в callback
 */
bsp_status_t bsp_can_register_rx_callback(bsp_can_rx_callback_t callback,
                                           void *p_user_ctx);
```

---

## Этап 2 — Реализация: `bsp/can/src/bsp_can.c`

Ключевые решения по реализации:

**Message Buffers (MB) FlexCAN2:**

| MB    | Назначение                        |
|-------|-----------------------------------|
| 0–5   | TX (отправка, round-robin)        |
| 6–21  | RX с индивидуальными фильтрами   |
| 22–31 | Резерв                            |

**Polling vs Callback — взаимоисключающие:**

```c
static bsp_can_rx_callback_t s_rx_callback;
static void *s_rx_user_ctx;
static volatile bool s_use_callback;

/* Внутренний ISR-обработчик */
static void can_rx_isr_handler(const bsp_can_frame_t *p_frame)
{
    if (s_use_callback && s_rx_callback != NULL) {
        s_rx_callback(p_frame, s_rx_user_ctx);
    } else {
        /* положить в internal ring buffer для polling */
        ring_buffer_write(&s_rx_ring, p_frame, sizeof(*p_frame));
    }
}
```

**Внутренний RX ring buffer** для polling-режима — переиспользуем `utils/ring_buffer/`.

**SDK-функции, которые будем вызывать (нужны для мока):**

```
FLEXCAN_Init()
FLEXCAN_Deinit()
FLEXCAN_SetTimingConfig()
FLEXCAN_SetRxMbConfig()
FLEXCAN_SetTxMbConfig()
FLEXCAN_SetRxIndividualMask()
FLEXCAN_TransferSendBlocking()    /* для TX */
FLEXCAN_TransferReceiveBlocking() /* для RX polling */
FLEXCAN_EnableMbInterrupts()
FLEXCAN_DisableMbInterrupts()
FLEXCAN_ReadRxMb()
```

---

## Этап 3 — CMake: `bsp/can/CMakeLists.txt`

```cmake
add_library(bsp_can STATIC
    src/bsp_can.c
)

target_include_directories(bsp_can
    PUBLIC  include
    PRIVATE ${BSP_GENERATED_DIR}
)

target_link_libraries(bsp_can
    PUBLIC  bsp_common
    PRIVATE bsp_tick          # для таймаутов
            ring_buffer       # внутренний RX буфер
)
```

Подключить в `bsp/CMakeLists.txt`:

```cmake
add_subdirectory(can)
```

---

## Этап 4 — Host-тесты

### 4.1 Stub-хедер: `tests/host/mocks/fsl_flexcan.h`

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Минимальные типы для компиляции bsp_can.c на хосте */

typedef struct { uint32_t reserved[256]; } CAN_Type;

typedef enum {
    kStatus_Success = 0,
    kStatus_Fail    = 1,
    kStatus_FLEXCAN_RxOverflow = 100,
} status_t;

typedef enum {
    kFLEXCAN_FrameFormatStandard = 0,
    kFLEXCAN_FrameFormatExtend   = 1,
} flexcan_frame_format_e;

typedef enum {
    kFLEXCAN_FrameTypeData   = 0,
    kFLEXCAN_FrameTypeRemote = 1,
} flexcan_frame_type_e;

typedef struct {
    uint32_t baudRate;
} flexcan_config_s;

typedef struct {
    uint32_t propSeg;
    uint32_t phaseSeg1;
    uint32_t phaseSeg2;
    uint32_t rJumpwidth;
    uint32_t preDivider;
} flexcan_timing_config_s;

typedef struct {
    volatile uint32_t id;
    volatile uint32_t cs;
    volatile uint8_t  data[8];
    volatile uint8_t  dlc;
} flexcan_frame_s;

typedef struct {
    uint32_t id;
    flexcan_frame_format_e format;
    flexcan_frame_type_e   type;
} flexcan_rx_mb_config_s;

typedef struct {
    flexcan_frame_s *p_frame;
    uint8_t          mb_idx;
} flexcan_mb_transfer_s;

/* Сигнатуры — реализации предоставляет fff */
void     FLEXCAN_Init(CAN_Type *p_base, const flexcan_config_s *p_config,
                      uint32_t src_clk);
void     FLEXCAN_Deinit(CAN_Type *p_base);
void     FLEXCAN_GetDefaultConfig(flexcan_config_s *p_config);
void     FLEXCAN_SetRxMbConfig(CAN_Type *p_base, uint8_t mb_idx,
                               const flexcan_rx_mb_config_s *p_config,
                               bool enable);
void     FLEXCAN_SetTxMbConfig(CAN_Type *p_base, uint8_t mb_idx, bool enable);
void     FLEXCAN_SetRxIndividualMask(CAN_Type *p_base, uint8_t mb_idx,
                                     uint32_t mask);
status_t FLEXCAN_TransferSendBlocking(CAN_Type *p_base, uint8_t mb_idx,
                                      flexcan_frame_s *p_frame);
status_t FLEXCAN_ReadRxMb(CAN_Type *p_base, uint8_t mb_idx,
                          flexcan_frame_s *p_frame);
void     FLEXCAN_EnableMbInterrupts(CAN_Type *p_base, uint32_t mask);
void     FLEXCAN_DisableMbInterrupts(CAN_Type *p_base, uint32_t mask);
uint32_t FLEXCAN_GetStatusFlags(CAN_Type *p_base);
void     FLEXCAN_ClearStatusFlags(CAN_Type *p_base, uint32_t mask);
```

### 4.2 Тестовый файл: `tests/host/can/test_bsp_can.c`

```c
#include "unity.h"
#include "fff.h"

DEFINE_FFF_GLOBALS;

#include "fsl_flexcan.h"

/* fff-фейки */
FAKE_VOID_FUNC(FLEXCAN_Init, CAN_Type *, const flexcan_config_s *, uint32_t);
FAKE_VOID_FUNC(FLEXCAN_Deinit, CAN_Type *);
FAKE_VOID_FUNC(FLEXCAN_GetDefaultConfig, flexcan_config_s *);
FAKE_VOID_FUNC(FLEXCAN_SetRxMbConfig, CAN_Type *, uint8_t,
               const flexcan_rx_mb_config_s *, bool);
FAKE_VOID_FUNC(FLEXCAN_SetTxMbConfig, CAN_Type *, uint8_t, bool);
FAKE_VOID_FUNC(FLEXCAN_SetRxIndividualMask, CAN_Type *, uint8_t, uint32_t);
FAKE_VALUE_FUNC(status_t, FLEXCAN_TransferSendBlocking, CAN_Type *, uint8_t,
                flexcan_frame_s *);
FAKE_VALUE_FUNC(status_t, FLEXCAN_ReadRxMb, CAN_Type *, uint8_t,
                flexcan_frame_s *);

#include "bsp/can.h"

void setUp(void)
{
    RESET_FAKE(FLEXCAN_Init);
    RESET_FAKE(FLEXCAN_Deinit);
    RESET_FAKE(FLEXCAN_GetDefaultConfig);
    RESET_FAKE(FLEXCAN_SetRxMbConfig);
    RESET_FAKE(FLEXCAN_SetTxMbConfig);
    RESET_FAKE(FLEXCAN_SetRxIndividualMask);
    RESET_FAKE(FLEXCAN_TransferSendBlocking);
    RESET_FAKE(FLEXCAN_ReadRxMb);
    FFF_RESET_HISTORY();
}

void tearDown(void) { }

/* ── Init ── */

void test_init_calls_flexcan_init(void)
{
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_status_t status = bsp_can_init(&cfg);

    TEST_ASSERT_EQUAL(BSP_OK, status);
    TEST_ASSERT_EQUAL(1, FLEXCAN_Init_fake.call_count);
}

void test_init_null_config_returns_err(void)
{
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_init(NULL));
}

void test_deinit_calls_flexcan_deinit(void)
{
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_can_init(&cfg);
    bsp_can_deinit();

    TEST_ASSERT_EQUAL(1, FLEXCAN_Deinit_fake.call_count);
}

/* ── TX ── */

void test_send_valid_frame(void)
{
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_can_init(&cfg);

    FLEXCAN_TransferSendBlocking_fake.return_val = kStatus_Success;

    bsp_can_frame_t frame = {
        .id = 0x123, .dlc = 8, .is_extended = false, .is_remote = false,
        .data = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04}
    };

    TEST_ASSERT_EQUAL(BSP_OK, bsp_can_send(&frame, 100U));
    TEST_ASSERT_EQUAL(1, FLEXCAN_TransferSendBlocking_fake.call_count);
}

void test_send_null_frame_returns_err(void)
{
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_can_init(&cfg);

    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_send(NULL, 100U));
}

void test_send_dlc_over_8_returns_err(void)
{
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_can_init(&cfg);

    bsp_can_frame_t frame = { .id = 0x123, .dlc = 9 };
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_send(&frame, 100U));
}

/* ── Фильтры ── */

void test_set_filter_std(void)
{
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_can_init(&cfg);

    bsp_status_t s = bsp_can_set_filter(0, 0x123, 0x7FFU, false);
    TEST_ASSERT_EQUAL(BSP_OK, s);
    TEST_ASSERT_GREATER_THAN(0, FLEXCAN_SetRxMbConfig_fake.call_count);
    TEST_ASSERT_GREATER_THAN(0, FLEXCAN_SetRxIndividualMask_fake.call_count);
}

void test_set_filter_index_out_of_range(void)
{
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_can_init(&cfg);

    TEST_ASSERT_EQUAL(BSP_ERR_PARAM,
                      bsp_can_set_filter(BSP_CAN_FILTER_MAX, 0x123, 0x7FF, false));
}

void test_accept_all_configures_all_mb(void)
{
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_can_init(&cfg);

    TEST_ASSERT_EQUAL(BSP_OK, bsp_can_accept_all());
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_calls_flexcan_init);
    RUN_TEST(test_init_null_config_returns_err);
    RUN_TEST(test_deinit_calls_flexcan_deinit);
    RUN_TEST(test_send_valid_frame);
    RUN_TEST(test_send_null_frame_returns_err);
    RUN_TEST(test_send_dlc_over_8_returns_err);
    RUN_TEST(test_set_filter_std);
    RUN_TEST(test_set_filter_index_out_of_range);
    RUN_TEST(test_accept_all_configures_all_mb);

    return UNITY_END();
}
```

### 4.3 Регистрация: `tests/host/CMakeLists.txt`

```cmake
add_host_test(
    NAME    test_bsp_can
    SOURCES can/test_bsp_can.c
            ${PROJECT_SOURCE_DIR}/bsp/can/src/bsp_can.c
            ${PROJECT_SOURCE_DIR}/utils/ring_buffer/ring_buffer.c
    INCLUDES
            ${PROJECT_SOURCE_DIR}/bsp/can/include
            ${PROJECT_SOURCE_DIR}/bsp/common/include
            ${PROJECT_SOURCE_DIR}/utils/ring_buffer
    MOCKS
            ${BSP_MOCKS_DIR}
)
```

### 4.4 `CMakePresets.json` — добавить в `host-debug-build`

```json
"targets": [
    "test_bsp_led",
    "test_log",
    "test_bsp_opto",
    "test_ring_buffer",
    "test_timeout_pattern",
    "uart_host_mock_example",
    "test_bsp_can"
]
```

---

## Этап 5 — HIL-тесты

### 5.1 Инфраструктура на стороне хоста: M5Stack CAN

M5Stack StamPLC с CAN-модулем (TJA1050 или MCP2515) работает как
CAN-адаптер. Связь host ↔ M5Stack через USB CDC (MicroPython).

**Агент на M5Stack** (`tools/hil/m5/can_agent.py` — MicroPython):

```
Команды через serial:
  CAN_INIT <bitrate>           → OK / ERR
  CAN_SEND <id> <dlc> <hex>   → OK / ERR
  CAN_RECV <timeout_ms>       → <id> <dlc> <hex> / TIMEOUT
  CAN_FILTER <id> <mask>      → OK / ERR
```

**Python-обёртка** (`tools/hil/m5/can_bus.py`):

```python
class M5CanBus:
    """CAN-адаптер через M5Stack serial."""

    def __init__(self, port: str, baudrate: int = 115200):
        self.ser = serial.Serial(port, baudrate, timeout=2.0)

    def init_can(self, bitrate: int = 500_000) -> None: ...
    def send(self, can_id: int, data: bytes,
             is_extended: bool = False) -> None: ...
    def recv(self, timeout_ms: int = 1000) -> CanFrame | None: ...
    def set_filter(self, can_id: int, mask: int) -> None: ...
```

### 5.2 C-прошивка: `tests/target/can/main.c`

```c
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "bsp/can.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CLI_BAUD_RATE  115200U
#define CLI_LINE_MAX   128U
#define CLI_RX_TIMEOUT 100U

static size_t cli_read_line(uint8_t *p_buf, size_t max_len) { /* как в шаблоне */ }

/* Вспомогательные функции парсинга */
static bool parse_hex_bytes(const char *p_hex, uint8_t *p_out, uint8_t len) { ... }
static void format_hex_bytes(const uint8_t *p_data, uint8_t len, char *p_out) { ... }

static void cli_process_line(const char *p_line)
{
    if (strncmp(p_line, "PING", 4U) == 0) {
        bsp_uart_host_write_str("PONG\r\n");
    }
    /* CAN_INIT <bitrate> */
    else if (strncmp(p_line, "CAN_INIT ", 9U) == 0) {
        uint32_t bitrate = (uint32_t)strtoul(p_line + 9, NULL, 10);
        bsp_can_config_t cfg = { .bitrate = bitrate };
        bsp_status_t s = bsp_can_init(&cfg);
        bsp_uart_host_write_str(s == BSP_OK ? "OK\r\n" : "ERR\r\n");
    }
    /* CAN_SEND <id_hex> <dlc> <data_hex> */
    else if (strncmp(p_line, "CAN_SEND ", 9U) == 0) {
        /* парсинг id, dlc, data из строки */
        bsp_can_frame_t frame = { /* заполнить */ };
        bsp_status_t s = bsp_can_send(&frame, 500U);
        bsp_uart_host_write_str(s == BSP_OK ? "OK\r\n" : "ERR\r\n");
    }
    /* CAN_RECV <timeout_ms> */
    else if (strncmp(p_line, "CAN_RECV ", 9U) == 0) {
        uint32_t timeout = (uint32_t)strtoul(p_line + 9, NULL, 10);
        bsp_can_frame_t frame;
        if (bsp_can_receive(&frame, timeout) == BSP_OK) {
            char buf[64];
            /* формат: <id_hex> <dlc> <data_hex> */
            bsp_uart_host_write_str(buf);
            bsp_uart_host_write_str("\r\n");
        } else {
            bsp_uart_host_write_str("TIMEOUT\r\n");
        }
    }
    /* CAN_FILTER <index> <id_hex> <mask_hex> <ext:0|1> */
    else if (strncmp(p_line, "CAN_FILTER ", 11U) == 0) {
        /* парсинг index, id, mask, is_extended */
        bsp_status_t s = bsp_can_set_filter(/* ... */);
        bsp_uart_host_write_str(s == BSP_OK ? "OK\r\n" : "ERR\r\n");
    }
    /* CAN_ACCEPT_ALL */
    else if (strncmp(p_line, "CAN_ACCEPT_ALL", 14U) == 0) {
        bsp_status_t s = bsp_can_accept_all();
        bsp_uart_host_write_str(s == BSP_OK ? "OK\r\n" : "ERR\r\n");
    }
    else if (p_line[0] != '\0') {
        bsp_uart_host_write_str("ERR_UNKNOWN\r\n");
    }
}

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    bsp_uart_host_init(CLI_BAUD_RATE);
    bsp_led_on(LED_HEARTBEAT);

    while (bsp_uart_host_rx_available() == 0U) {
        bsp_uart_host_write_str("READY\r\n");
        bsp_delay(200U);
    }

    static uint8_t s_line_buf[CLI_LINE_MAX];
    for (;;) {
        size_t len = cli_read_line(s_line_buf, sizeof(s_line_buf));
        if (len > 0U) {
            cli_process_line((const char *)s_line_buf);
        }
    }
}
```

### 5.3 CMake: `tests/target/can/CMakeLists.txt`

```cmake
set(TARGET_NAME test_hil_can)

add_executable(${TARGET_NAME}
    main.c
    ${BSP_GENERATED}/clock_config.c
    ${BSP_STARTUP_FILE}
    ${BSP_SYSCALLS_FILE}
)

target_link_options(${TARGET_NAME} PRIVATE
    -T${CMAKE_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_ram.ld
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.map
)

target_link_libraries(${TARGET_NAME} PRIVATE
    bsp_boot_ram
    bsp_board
    bsp_led
    bsp_tick
    bsp_uart_host
    bsp_can
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
    COMMENT "Size: ${TARGET_NAME}"
)
```

### 5.4 Подключить в `tests/target/CMakeLists.txt`

```cmake
add_subdirectory(host_uart)
add_subdirectory(can)          # ← добавить
```

### 5.5 `CMakePresets.json` — добавить в `target-debug-build`

```json
"targets": [
    "test_host_uart",
    "test_hil_opto",
    "test_hil_can"
]
```

### 5.6 Pytest: `tools/hil/test_can.py`

```python
"""test_can.py — HIL тесты bsp_can."""
import pytest
import time
from conftest import uart_cmd
from m5.can_bus import M5CanBus


@pytest.fixture(scope="module")
def can_bus(request) -> M5CanBus:
    """CAN-адаптер на стороне хоста (M5Stack)."""
    port = request.config.getoption("--m5-port")
    bus = M5CanBus(port)
    bus.init_can(500_000)
    yield bus
    bus.close()


class TestCanBasic:

    @pytest.fixture(autouse=True)
    def _setup(self, loaded_can, uart, can_bus):
        self.ser = uart
        self.can = can_bus
        # Инициализировать CAN на MCU
        assert uart_cmd(self.ser, "CAN_INIT 500000") == "OK"
        assert uart_cmd(self.ser, "CAN_ACCEPT_ALL") == "OK"

    @pytest.mark.smoke
    def test_ping(self):
        """Базовая проверка UART-канала."""
        assert uart_cmd(self.ser, "PING") == "PONG"

    def test_mcu_tx_host_rx(self):
        """MCU отправляет фрейм, M5 принимает."""
        assert uart_cmd(self.ser, "CAN_SEND 123 8 DEADBEEF01020304") == "OK"
        frame = self.can.recv(timeout_ms=1000)
        assert frame is not None
        assert frame.id == 0x123
        assert frame.dlc == 8
        assert frame.data == bytes.fromhex("DEADBEEF01020304")

    def test_host_tx_mcu_rx(self):
        """M5 отправляет фрейм, MCU принимает."""
        self.can.send(0x456, bytes.fromhex("AABBCCDD"), is_extended=False)
        resp = uart_cmd(self.ser, "CAN_RECV 1000")
        assert resp != "TIMEOUT"
        # Парсинг ответа: "456 4 AABBCCDD"
        parts = resp.split()
        assert int(parts[0], 16) == 0x456
        assert int(parts[1]) == 4

    def test_loopback_echo(self):
        """M5 отправляет → MCU принимает → MCU отправляет назад → M5 принимает."""
        self.can.send(0x100, bytes.fromhex("01020304"))
        # MCU должен принять
        resp = uart_cmd(self.ser, "CAN_RECV 1000")
        assert resp != "TIMEOUT"
        # MCU отправляет эхо
        assert uart_cmd(self.ser, "CAN_SEND 100 4 01020304") == "OK"
        echo = self.can.recv(timeout_ms=1000)
        assert echo is not None
        assert echo.id == 0x100


class TestCanFilter:

    @pytest.fixture(autouse=True)
    def _setup(self, loaded_can, uart, can_bus):
        self.ser = uart
        self.can = can_bus
        assert uart_cmd(self.ser, "CAN_INIT 500000") == "OK"

    def test_filter_accepts_matching_id(self):
        """Фильтр 0x200/0x7FF — принимает 0x200."""
        assert uart_cmd(self.ser, "CAN_FILTER 0 200 7FF 0") == "OK"
        self.can.send(0x200, bytes(4))
        resp = uart_cmd(self.ser, "CAN_RECV 1000")
        assert resp != "TIMEOUT"
        assert resp.startswith("200 ")

    def test_filter_rejects_non_matching_id(self):
        """Фильтр 0x200/0x7FF — отвергает 0x300."""
        assert uart_cmd(self.ser, "CAN_FILTER 0 200 7FF 0") == "OK"
        self.can.send(0x300, bytes(4))
        resp = uart_cmd(self.ser, "CAN_RECV 500")
        assert resp == "TIMEOUT"

    def test_filter_mask_partial(self):
        """Маска 0x7F0 — принимает 0x201..0x20F."""
        assert uart_cmd(self.ser, "CAN_FILTER 0 200 7F0 0") == "OK"
        self.can.send(0x205, bytes(2))
        resp = uart_cmd(self.ser, "CAN_RECV 1000")
        assert resp != "TIMEOUT"

    def test_accept_all_after_filter(self):
        """accept_all сбрасывает фильтры."""
        assert uart_cmd(self.ser, "CAN_FILTER 0 200 7FF 0") == "OK"
        assert uart_cmd(self.ser, "CAN_ACCEPT_ALL") == "OK"
        self.can.send(0x300, bytes(4))
        resp = uart_cmd(self.ser, "CAN_RECV 1000")
        assert resp != "TIMEOUT"


class TestCanExtended:

    @pytest.fixture(autouse=True)
    def _setup(self, loaded_can, uart, can_bus):
        self.ser = uart
        self.can = can_bus
        assert uart_cmd(self.ser, "CAN_INIT 500000") == "OK"
        assert uart_cmd(self.ser, "CAN_ACCEPT_ALL") == "OK"

    def test_ext_id_tx_rx(self):
        """Отправка/приём EXT ID (29-bit)."""
        assert uart_cmd(self.ser,
                        "CAN_SEND 1ABCDEF0 4 AABBCCDD") == "OK"
        frame = self.can.recv(timeout_ms=1000)
        assert frame is not None
        assert frame.id == 0x1ABCDEF0
        assert frame.is_extended is True
```

### 5.7 Фикстура загрузки: `tools/hil/conftest.py`

```python
@pytest.fixture(scope="module")
def loaded_can(request: pytest.FixtureRequest) -> None:
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/can/test_hil_can.elf",
    )
```

### 5.8 `just/host.just` — алиасы

```just
[doc('Запустить HIL-тест CAN')]
[group('hil')]
hil-can:
    HIL_BUILD_DIR={{_hil_build}} \
        uv run --directory {{HIL_DIR}} pytest test_can.py -v
```

---

## Этап 6 (будущее) — Callback для FreeRTOS

Не входит в первую итерацию. Заглушка в API уже есть (`bsp_can_register_rx_callback`).
Реализация в `firmware/tft_app/`:

```c
static QueueHandle_t s_can_queue;

static void can_isr_to_queue(const bsp_can_frame_t *p_frame, void *p_ctx)
{
    BaseType_t higher_woken = pdFALSE;
    xQueueSendFromISR(s_can_queue, p_frame, &higher_woken);
    portYIELD_FROM_ISR(higher_woken);
}

void can_task(void *p_param)
{
    s_can_queue = xQueueCreate(16, sizeof(bsp_can_frame_t));
    bsp_can_config_t cfg = { .bitrate = 500000U };
    bsp_can_init(&cfg);
    bsp_can_register_rx_callback(can_isr_to_queue, NULL);

    bsp_can_frame_t rx;
    for (;;) {
        if (xQueueReceive(s_can_queue, &rx, portMAX_DELAY) == pdTRUE) {
            /* dispatch по rx.id */
        }
    }
}
```

---

## Файловая карта изменений

```
НОВЫЕ ФАЙЛЫ:
  bsp/can/CMakeLists.txt
  bsp/can/include/bsp/can.h
  bsp/can/src/bsp_can.c
  tests/host/can/test_bsp_can.c
  tests/host/mocks/fsl_flexcan.h
  tests/target/can/main.c
  tests/target/can/CMakeLists.txt
  tools/hil/test_can.py
  tools/hil/m5/can_agent.py          (MicroPython на M5Stack)
  tools/hil/m5/can_bus.py            (Python-обёртка)

ИЗМЕНЕНИЯ В СУЩЕСТВУЮЩИХ ФАЙЛАХ:
  bsp/CMakeLists.txt                  ← add_subdirectory(can)
  tests/host/CMakeLists.txt           ← add_host_test(... test_bsp_can ...)
  tests/target/CMakeLists.txt         ← add_subdirectory(can)
  CMakePresets.json                   ← test_bsp_can в host-debug-build
                                      ← test_hil_can в target-debug-build
  tools/hil/conftest.py               ← loaded_can фикстура
  just/host.just                      ← hil-can алиас
```

---

## Чеклист реализации

```
Этап 1–2: BSP модуль
  [ ] bsp/can/include/bsp/can.h         — публичный API
  [ ] bsp/can/src/bsp_can.c             — реализация (FlexCAN2)
  [ ] bsp/can/CMakeLists.txt            — библиотека
  [ ] bsp/CMakeLists.txt                — add_subdirectory(can)

Этап 3: pin_mux
  [ ] bsp/generated/pin_mux.*           — добавить FLEXCAN2 TX/RX в .mex

Этап 4: Host-тесты
  [ ] tests/host/mocks/fsl_flexcan.h    — stub
  [ ] tests/host/can/test_bsp_can.c     — тесты
  [ ] tests/host/CMakeLists.txt         — add_host_test
  [ ] CMakePresets.json                 — host-debug-build targets
  [ ] just build::test-host             — зелёный прогон

Этап 5: HIL-тесты
  [ ] tests/target/can/main.c           — C-прошивка с CLI
  [ ] tests/target/can/CMakeLists.txt   — сборка
  [ ] tests/target/CMakeLists.txt       — add_subdirectory
  [ ] CMakePresets.json                 — target-debug-build targets
  [ ] tools/hil/m5/can_agent.py         — MicroPython агент
  [ ] tools/hil/m5/can_bus.py           — Python-обёртка
  [ ] tools/hil/test_can.py             — pytest
  [ ] tools/hil/conftest.py             — loaded_can фикстура
  [ ] just/host.just                    — hil-can алиас
  [ ] just build::build-hil             — сборка target-прошивки
  [ ] just host::hil-can                — зелёный прогон
```

---

## Порядок работы

```bash
# 1. Создать bsp/can (API + реализация)
# 2. Добавить pin_mux для FLEXCAN2 в .mex → перегенерировать
# 3. Host-тесты: stub + test + CMake → just build::test-host
# 4. HIL C-прошивка: target/can → just build::build-hil
# 5. M5Stack CAN-агент: залить can_agent.py
# 6. HIL pytest: test_can.py → just host::hil-can
# 7. Интегрировать в firmware/test (CLI-команды CAN)
```