# bsp_usb_cdc — USB CDC ACM (Virtual COM Port)

USB CDC ACM device на USB1 (EHCI0). Хост видит устройство как виртуальный COM-порт
(`/dev/ttyACM*` на Linux/macOS, `COMx` на Windows).

Используется для передачи данных между платой и ПК: отладочные лог-каналы,
CLI команды, обновление конфигурации. Работает параллельно с `bsp_uart_host`
(LPUART1) — два независимых канала.

---

## Аппаратура

| Сигнал        | Пин MCU       | Назначение                 |
| ------------- | ------------- | -------------------------- |
| USB_OTG1_DN   | USB_OTG1_DN   | USB1 Data−                 |
| USB_OTG1_DP   | USB_OTG1_DP   | USB1 Data+                 |
| USB_OTG1_VBUS | USB_OTG1_VBUS | VBUS detect (self-powered) |

Встроенный HS PHY (480 MHz PLL). Контроллер: EHCI0 (`kUSB_ControllerEhci0`).
Скорость: High-Speed (480 Mbit/s) при поддержке хоста, fallback Full-Speed (12 Mbit/s).

USB PHY калибровка: `D_CAL=0x0C`, `TXCAL45DP=0x06`, `TXCAL45DM=0x06` — стандартные
значения для EVKB, подходят для кабелей до 3 м.

**VID/PID**: `0x1234` / `0x0001` (placeholder, заменить на производственные).

---

## Архитектура

```bash
                              bsp_usb_cdc_write()
                                     ↓
                          memcpy → s_sendBuf (NonCacheable OCRAM)
                                     ↓
                          USB_DeviceSendRequest()
                                     ↓
                   [EHCI0 DMA] → USB1_DP/DN → Host

Host → USB1_DP/DN → [EHCI0 DMA]
                          ↓
              USB_OTG1_IRQHandler → BulkOut callback
                          ↓
                    s_recvBuf (NonCacheable OCRAM)
                          ↓
                   s_recvSize = len (volatile)
                          ↓
                  bsp_usb_cdc_read()  ← main loop polling
```

Все DMA-буферы (`s_sendBuf`, `s_recvBuf`, дескрипторы) размещены в секции
`NonCacheable` (OCRAM `0x20200000`). MPU region 9 настраивает эту область
как Normal non-cacheable — записи CPU видны DMA без `SCB_CleanDCache()`.

---

## USB стек — lite архитектура

Модуль использует **lite** вариант NXP USB стека (не full class framework).
Это сознательное решение:

| Аспект             | Full stack                             | Lite stack (наш выбор)        |
| ------------------ | -------------------------------------- | ----------------------------- |
| Class framework    | `usb_device_class.h`, `class_handle_t` | Отсутствует                   |
| `usb_device_ch9.c` | SDK middleware, тянет class driver     | Приватная копия в `src/`      |
| CDC ACM хедер      | Полный: struct + API функции           | Только define-ы request codes |
| Callbacks          | Через class driver dispatch            | Напрямую в `usb_cdc.c`        |
| Размер кода        | ~12 KB                                 | ~6 KB                         |
| Гибкость           | Multi-class composite                  | Один CDC ACM                  |

Lite stack достаточен для одного CDC ACM интерфейса. Переход на full stack
понадобится только при добавлении composite device (CDC + MSC).

### Стек зависимостей

```bash
bsp_usb_cdc
├── src/usb_cdc.c              ← BSP API + USB device callbacks
├── src/usb_cdc_descriptors.c  ← дескрипторы + descriptor callbacks
├── src/usb_cdc_hw.c           ← clock, PHY, IRQ handler
├── src/usb_device_ch9.c       ← lite Chapter 9 (приватная копия)
│
├── SDK (PRIVATE):
│   ├── sdk_usb_device_ehci    ← EHCI контроллер + DCI абстракция
│   │   ├── usb_device_ehci.c
│   │   └── usb_device_dci.c
│   ├── sdk_usb_phy            ← USB PHY инициализация
│   │   └── usb_phy.c
│   └── sdk_osa_bm             ← OS Abstraction (bare-metal)
│       ├── fsl_os_abstraction_bm.c
│       └── fsl_component_generic_list.c
│
└── Приватные конфиги в src/:
    ├── usb_device_config.h          ← EHCI=1, CDC_ACM=1, endpoints=4
    ├── fsl_os_abstraction_config.h  ← bare-metal OSA конфиг
    ├── usb_device_descriptor.h      ← VID/PID, endpoint numbers
    ├── usb_device_ch9.h             ← lite ch9 API (1 arg)
    └── usb_device_cdc_acm.h         ← lite: только CDC request codes
```

### Проброс конфиг-хедеров (sdk_usb_config)

NXP USB middleware при компиляции ищет `usb_device_config.h` и
`fsl_os_abstraction_config.h` через include path. Эти файлы —
application-specific, живут в `bsp/usb_cdc/src/`.

Проблема: SDK таргеты (`sdk_usb_device_ehci`, `sdk_usb_phy`, `sdk_osa_bm`)
компилируются независимо от `bsp_usb_cdc` и не видят его include paths.

Решение: INTERFACE библиотека `sdk_usb_config` в `sdk/CMakeLists.txt`:

```cmake
add_library(sdk_usb_config INTERFACE)
target_include_directories(sdk_usb_config SYSTEM
    INTERFACE ${CMAKE_SOURCE_DIR}/bsp/usb_cdc/src)
```

Все SDK USB таргеты линкуют `sdk_usb_config` и находят конфиг-хедеры при
компиляции. Циклических зависимостей нет — `sdk_usb_config` не содержит кода.

---

## Быстрый старт

```c
#include "bsp/usb_cdc.h"

/* После board_hw_init() + bsp_tick_init(): */
bsp_usb_cdc_init();

/* Ждём подключения хоста */
while (!bsp_usb_cdc_is_ready()) {
    /* USB enumeration в процессе */
}

/* TX — неблокирующая отправка */
const char *msg = "Hello from TFT Board\r\n";
bsp_usb_cdc_write((const uint8_t *)msg, strlen(msg));

/* RX — polling в main loop */
uint8_t buf[64];
size_t n = bsp_usb_cdc_read(buf, sizeof(buf));
if (n > 0) {
    /* обработать buf[0..n-1] */
}
```

---

## API

### `bsp_usb_cdc_init()`

Полная инициализация: USB PHY clock 480 MHz → EHCI0 init → endpoint registration →
NVIC enable → USB_DeviceRun. Включает задержку 5 мс для стабилизации DP pull-down.

**Предусловие**: `board_hw_init()` вызван (MPU настроен, NonCacheable регион активен).

Возвращает `BSP_OK` или `BSP_ERR_HW`.

### `bsp_usb_cdc_is_ready()`

`true` когда USB enumeration завершён **и** хост открыл COM-порт (DTR установлен
через `SET_CONTROL_LINE_STATE`). До этого момента `write()` вернёт `BSP_ERR_NOT_READY`.

### `bsp_usb_cdc_write(data, len)`

Неблокирующая отправка. Копирует данные в NonCacheable TX буфер и ставит в очередь
USB IN transfer. Максимум `BSP_USB_CDC_MAX_PACKET_SIZE` (512) байт за вызов.

| Возврат             | Условие                                    |
| ------------------- | ------------------------------------------ |
| `BSP_OK`            | Transfer поставлен в очередь               |
| `BSP_ERR_BUSY`      | Предыдущий transfer не завершён            |
| `BSP_ERR_NOT_READY` | Хост не подключён                          |
| `BSP_ERR_INVALID`   | `data == NULL`, `len == 0` или `len > 512` |

Проверить готовность TX канала перед отправкой: `bsp_usb_cdc_write_ready()`.

### `bsp_usb_cdc_write_ready()`

`true` если предыдущий TX transfer завершён и хост подключён.
Удобно для non-blocking write loop:

```c
if (bsp_usb_cdc_write_ready()) {
    bsp_usb_cdc_write(data, len);
}
```

### `bsp_usb_cdc_read(buf, max_len)`

Неблокирующее чтение. Забирает данные из RX буфера, заполненного USB OUT ISR callback.
Автоматически перепланирует следующий OUT transfer. Возвращает количество прочитанных
байт (0 если данных нет).

```c
/* Polling в main loop: */
uint8_t buf[64];
size_t n = bsp_usb_cdc_read(buf, sizeof(buf));
```

### `bsp_usb_cdc_poll()`

Зарезервировано. Для bare-metal на EHCI NXP стек обрабатывает всё в ISR.
Для будущего использования с `USB_DEVICE_CONFIG_USE_TASK`.

---

## NonCacheable память

USB EHCI DMA требует некэшируемые буферы. Модуль размещает буферы через макросы
`USB_DMA_INIT_DATA_ALIGN()` и `USB_DMA_NONINIT_DATA_ALIGN()`, которые помещают
данные в секции `NonCacheable.init` и `NonCacheable`.

Линкер-скрипт размещает эти секции в OCRAM (`m_data2`, `0x20200000`).
`board_mpu_init()` настраивает MPU region 9 для этой области.

Проверка: `firmware_test/main.c` содержит `ncache_test_run()` — верификация
что NonCacheable буфер физически попадает в ожидаемый регион.

**Объём**: ~2.5 KB (два bulk буфера по 512 байт + дескрипторы + ACM info +
setup buffer). При NonCacheable регионе 8 KB запас достаточный.

---

## ISR и синхронизация

```bash
USB_OTG1_IRQHandler  (usb_cdc_hw.c)
    └── USB_DeviceEhciIsrFunction()  (SDK)
            ├── BulkOut callback → s_recvSize = len  (volatile)
            ├── BulkIn callback  → s_txIdle = 1      (volatile)
            └── DeviceCallback   → s_cdcState.attach  (volatile)
```

Синхронизация между ISR и main loop:

- **RX**: `bsp_usb_cdc_read()` входит в critical section (`DisableGlobalIRQ`),
  копирует `s_recvSize`, сбрасывает в 0, выходит. Копирование из `s_recvBuf`
  происходит после выхода из critical section.
- **TX**: `s_txIdle` — volatile flag, устанавливается в BulkIn callback (ISR),
  проверяется в `bsp_usb_cdc_write()` (main loop). Гонка исключена: write
  сбрасывает flag перед `USB_DeviceSendRequest`.

---

## FreeRTOS

Модуль работает без изменений в контексте FreeRTOS-задачи:

| Контекст   | TX                                          | RX                             |
| ---------- | ------------------------------------------- | ------------------------------ |
| bare-metal | `bsp_usb_cdc_write()` — non-blocking        | `bsp_usb_cdc_read()` — polling |
| FreeRTOS   | Из задачи, `write_ready()` + `vTaskDelay()` | Из задачи с yield              |

Для минимальной латентности в FreeRTOS — будущий `USB_DEVICE_CONFIG_USE_TASK=1`
с `bsp_usb_cdc_poll()` из выделенной задачи.

`USB_DEVICE_INTERRUPT_PRIORITY` (3) должен быть ниже
`configMAX_SYSCALL_INTERRUPT_PRIORITY` при использовании FreeRTOS API из ISR.

---

## Подключение

```cmake
# bsp/CMakeLists.txt — уже добавлено
add_subdirectory(usb_cdc)

# firmware/test/CMakeLists.txt
target_link_libraries(firmware_test PRIVATE
    bsp_board
    bsp_tick
    bsp_usb_cdc
)
```

---

## Тестирование

### HIL-тест

USB CDC появляется как второй COM-порт на хосте (помимо MCU-Link VCOM).
C-прошивка `tests/target/hil_usb_cdc/` — CLI через USB CDC.
pytest: `tools/hil/test_usb_cdc.py` — отправка/приём через pyserial.

Переменная окружения `HIL_USB_CDC_PORT` — порт USB CDC устройства таргета.

```bash
just host::hil-usb-cdc
```

Команды CLI прошивки:

| Команда       | Ответ    | Описание         |
| ------------- | -------- | ---------------- |
| `PING`        | `PONG`   | Проверка канала  |
| `ECHO <data>` | `<data>` | Echo-back данных |

### Host unit-тесты

Не применяются — модуль полностью завязан на USB hardware и NXP middleware.
Тестирование только через HIL.

---

## Конфигурация

Все настройки находятся в приватных хедерах `src/`:

| Файл                      | Настройка                       | Значение | Описание                         |
| ------------------------- | ------------------------------- | -------- | -------------------------------- |
| `usb_device_config.h`     | `USB_DEVICE_CONFIG_EHCI`        | `1`      | Контроллер EHCI0                 |
| `usb_device_config.h`     | `USB_DEVICE_CONFIG_ENDPOINTS`   | `4`      | EP0 + interrupt IN + bulk IN/OUT |
| `usb_device_config.h`     | `USB_DEVICE_CONFIG_SELF_POWER`  | `1`      | Self-powered device              |
| `usb_device_descriptor.h` | `USB_DEVICE_VID`                | `0x1234` | Vendor ID (placeholder)          |
| `usb_device_descriptor.h` | `USB_DEVICE_PID`                | `0x0001` | Product ID (placeholder)         |
| `usb_cdc_hw.c`            | `USB_DEVICE_INTERRUPT_PRIORITY` | `3`      | NVIC приоритет                   |
| `usb_cdc_hw.c`            | `BOARD_USB_PHY_D_CAL`           | `0x0C`   | PHY калибровка                   |

---

## Файловая структура

```bash
bsp/usb_cdc/
├── CMakeLists.txt
├── README.md
├── include/
│   └── bsp/
│       └── usb_cdc.h                  # публичный API — без NXP хедеров
└── src/
    ├── usb_cdc.c                      # BSP API + USB device callbacks
    ├── usb_cdc_descriptors.c          # дескрипторы + descriptor callbacks
    ├── usb_cdc_hw.c                   # clock, PHY init, IRQ handler
    ├── usb_device_ch9.c              # lite Chapter 9 (копия из NXP примера)
    ├── usb_device_ch9.h              # lite ch9 API
    ├── usb_device_cdc_acm.h          # lite: только CDC request codes
    ├── usb_device_config.h           # конфигурация USB стека
    ├── usb_device_descriptor.h       # VID/PID, endpoints, packet sizes
    └── fsl_os_abstraction_config.h   # OSA bare-metal конфиг
```

---

## Зависимости

| Зависимость           | Тип                   | Описание                                                |
| --------------------- | --------------------- | ------------------------------------------------------- |
| `bsp_status`          | PUBLIC                | `bsp_status_t` в публичном API                          |
| `bsp_board`           | PRIVATE               | Транзитивно: `clock_config.h`, `pin_mux.h`, SDK headers |
| `sdk_usb_device_ehci` | PRIVATE               | EHCI контроллер + DCI абстракция                        |
| `sdk_usb_phy`         | PRIVATE               | USB PHY инициализация (480 MHz PLL)                     |
| `sdk_osa_bm`          | PRIVATE               | OS Abstraction Layer (bare-metal, generic list)         |
| `sdk_usb_common`      | PRIVATE (транзитивно) | USB common headers (`usb.h`, `usb_misc.h`)              |
| `sdk_usb_config`      | PRIVATE (транзитивно) | INTERFACE: проброс конфиг-хедеров в SDK                 |

### Зависимости на уровне SDK CMake

```bash
sdk_usb_device_ehci ─┬─ sdk_usb_common ── sdk_osa_bm ── sdk_usb_config
                     └─ sdk_usb_config          │              │
                                            components/osa  bsp/usb_cdc/src/
sdk_usb_phy ── sdk_usb_common                components/lists  (конфиг-хедеры)
```

`sdk_usb_config` — INTERFACE библиотека без кода. Единственная роль —
прокинуть include path к `bsp/usb_cdc/src/` для SDK таргетов,
которым нужны `usb_device_config.h` и `fsl_os_abstraction_config.h`.
