# USB — теория для embedded-разработчика

> Памятка: фокус на CDC ACM (Virtual COM Port) для NXP IMXRT1052

---

## 1. Основы архитектуры USB

USB — это **master-slave** шина. Хост всегда инициирует обмен, устройство только отвечает. Никакой "самодеятельности" от устройства быть не может — только реакция на запросы хоста.

```bash
HOST (PC)                        DEVICE (MCU)
──────────                       ────────────
OS USB stack                     USB device stack
    ↕                                   ↕
Host controller (xHCI/EHCI)  ←→  Device controller (EHCI на IMXRT)
                         D+  D−  VBUS  GND
```

### Физический уровень

| Параметр | USB Full Speed | USB High Speed |
|----------|---------------|----------------|
| Скорость | 12 Мбит/с | 480 Мбит/с |
| IMXRT1052 | ✅ | ✅ |
| Практическая пропускная способность BULK | ~1 МБ/с | ~40 МБ/с |
| Применение | CDC ACM, HID | MSD, Video |

IMXRT1052 имеет два USB контроллера: `USB1` (OTG, EHCI) и `USB2` (Host only). Для CDC ACM используем `USB1`.

---

## 2. Ключевые понятия

### Дескрипторы

Дескрипторы — это набор структур, которые устройство возвращает хосту при подключении (в ответ на `GET_DESCRIPTOR`). Хост читает их и решает, какой драйвер загрузить.

```bash
Device Descriptor
└── Configuration Descriptor
    ├── Interface Descriptor #0  (CDC Control)
    │   ├── CDC Header Functional Descriptor
    │   ├── CDC Call Management Descriptor
    │   ├── CDC ACM Functional Descriptor
    │   ├── CDC Union Functional Descriptor
    │   └── Endpoint Descriptor (INT IN)
    └── Interface Descriptor #1  (CDC Data)
        ├── Endpoint Descriptor (BULK IN)
        └── Endpoint Descriptor (BULK OUT)
```

Важные поля `Device Descriptor`:

| Поле | Значение | Смысл |
|------|----------|-------|
| `bDeviceClass` | 0xEF | Composite (классы на уровне интерфейсов) |
| `idVendor` | 0x1FC9 | VID NXP (или свой) |
| `idProduct` | произвольный | PID — идентификатор продукта |
| `bcdUSB` | 0x0200 | USB 2.0 |

### Endpoints (эндпоинты)

Эндпоинт — это буфер в устройстве с определённым направлением и типом передачи. EP0 — всегда управляющий (Control), остальные — настраиваются.

| Тип | Гарантия доставки | Применение |
|-----|------------------|------------|
| Control | да | конфигурация устройства, EP0 |
| Bulk | да (retry) | большие данные, CDC ACM данные |
| Interrupt | да (периодически) | HID, CDC ACM нотификации |
| Isochronous | нет | аудио, видео |

**Для CDC ACM нужны три эндпоинта:**

```bash
EP0     Control IN/OUT   — управление (всегда есть, не конфигурируется)
EP1     INT IN           — нотификации CDC (DTR, RTS — наследие модемов)
EP2     BULK IN          — данные device → host (твои JSON-строки → PC)
EP3     BULK OUT         — данные host → device (команды PC → плата)
```

### Enumeration — что происходит при подключении кабеля

```bash
1.  Хост видит устройство (pull-up на D+)
2.  USB Reset (SE0, 10 мс)
3.  GET_DESCRIPTOR(Device) → хост узнаёт VID/PID, версию USB
4.  SET_ADDRESS → устройство получает адрес на шине (1–127)
5.  GET_DESCRIPTOR(Configuration) → хост видит интерфейсы
6.  GET_DESCRIPTOR(String) × N → имена для Device Manager
7.  SET_CONFIGURATION(1) → USB stack поднимает эндпоинты
        → на стороне устройства срабатывает callback kUSB_DeviceEventSetConfiguration
8.  Хост загружает драйвер по (bDeviceClass, idVendor, idProduct)
        → CDC ACM: cdc_acm.ko (Linux) / usbser.sys (Windows)
9.  Появляется /dev/ttyACM0 или COM3
10. Пользователь открывает порт → хост посылает SET_CONTROL_LINE_STATE с DTR=1
        → устройство видит "хост подключён"
```

Шаг 10 критически важен: **пока терминал не открыт — DTR = 0**. Слать данные до появления DTR бессмысленно — хост их не читает.

---

## 3. CDC ACM — детали класса

CDC (Communications Device Class) — класс для коммуникационных устройств. ACM (Abstract Control Model) — подкласс, изначально для модемов, сейчас стандарт де-факто для Virtual COM Port.

### Почему CDC ACM а не другие классы

| Класс | Что видит OS | Проблема |
|-------|-------------|----------|
| **CDC ACM** | `/dev/ttyACM0`, `COM3` | — нет, это и нужно |
| Vendor | ничего | нужен свой драйвер под каждую ОС |
| HID | `/dev/hidraw0` | пакет максимум 64 байта, неудобно |
| MSC | блочное устройство | совсем не то |

Главное преимущество CDC ACM: **стандартный драйвер есть везде** — Linux, Windows 10+, macOS — без установки чего-либо.

### Ограничения которые надо знать

**USB CDC не гарантирует границы сообщений.** Данные идут потоком через BULK-эндпоинты. Если ты послал `{"type":"result"}\n{"type":"summary"}\n` — хост может получить это как один кусок, два куска, или три куска произвольного размера.

Поэтому **всегда нужен frame delimiter**. В нашем проекте — символ `\n` (JSON-lines). Приёмная сторона буферизирует до `\n` и только тогда парсит JSON.

**Скорость** не ограничена физическими 115200 бод как у UART. USB Full Speed BULK даёт практически ~1 МБ/с. Baudrate в настройках терминала для CDC ACM — декоративный, реально на скорость не влияет.

---

## 4. NXP USB Stack на IMXRT1052

### Архитектура стека

```bash
твой код (bsp_usb_cdc)
    ↕  callbacks + API
usb_device_cdc_acm.c       ← CDC ACM класс (middleware/usb/device/class/)
    ↕
usb_device_dci.c           ← Device Controller Interface (middleware/usb/device/)
    ↕
usb_device_ehci.c          ← EHCI контроллер (middleware/usb/device/)
    ↕
USB PHY (usb_phy.c)        ← физический уровень (middleware/usb/phy/)
    ↕
EHCI hardware registers
```

### Callback-архитектура

NXP USB stack работает через callbacks — ты не вызываешь функции стека для приёма данных, стек сам вызывает твои функции когда что-то происходит.

Два уровня callbacks:

```c
/* 1. Callback уровня устройства — системные события */
usb_status_t USB_DeviceCallback(usb_device_handle handle,
                                uint32_t event,
                                void *param)
{
    switch (event) {
    case kUSB_DeviceEventBusReset:
        /* сброс шины — переинициализировать эндпоинты */
        break;
    case kUSB_DeviceEventSetConfiguration:
        /* хост завершил enumeration — можно начинать работать */
        break;
    }
}

/* 2. Callback уровня CDC ACM класса — данные и управление */
usb_status_t USB_DeviceCdcAcmCallback(class_handle_t handle,
                                      uint32_t event,
                                      void *param)
{
    switch (event) {
    case kUSB_DeviceCdcEventSendResponse:
        /* BULK IN передача завершена — буфер можно переиспользовать */
        break;
    case kUSB_DeviceCdcEventRecvResponse:
        /* BULK OUT данные получены — param указывает на буфер */
        break;
    case kUSB_DeviceCdcEventSetControlLineState:
        /* DTR/RTS изменились — проверяем подключение хоста */
        break;
    }
}
```

### usb_device_config.h — конфигурационный файл

NXP USB stack требует конфигурационный хедер. Он **не входит в SDK** — его пишешь ты и кладёшь в `bsp/usb_cdc/src/`. Ключевые параметры:

```c
/* bsp/usb_cdc/src/usb_device_config.h */

/* Тип контроллера: EHCI для IMXRT1052 */
#define USB_DEVICE_CONFIG_EHCI                    1

/* Включаем CDC ACM класс */
#define USB_DEVICE_CONFIG_CDC_ACM                 1

/* Количество одновременных CDC инстансов */
#define USB_DEVICE_CONFIG_CDC_ACM_INSTANCE_COUNT  1

/* Количество эндпоинтов (EP0 + INT + BULK IN + BULK OUT = 4) */
#define USB_DEVICE_CONFIG_ENDPOINTS               4

/* Размер BULK буферов (степень двойки, FS max = 64 байта на транзакцию,
   но можно использовать большие буферы для нескольких транзакций) */
#define USB_DEVICE_CONFIG_CDC_ACM_MAX_DATAPIPE_SIZE  512

/* Bare-metal (без RTOS) */
#define USB_DEVICE_CONFIG_USE_TASK                0
```

### IRQ и polling

На IMXRT1052 USB работает через прерывания. Стек нужно "тикать" из ISR:

```c
/* в startup или IRQ handler регистрации */
void USB_OTG1_IRQHandler(void) {
    USB_DeviceEhciIsrFunction(g_usb_device_handle);
}
```

В bare-metal также нужен периодический вызов `USB_DeviceTaskFunction()` из main loop — он обрабатывает отложенные события которые нельзя делать прямо в ISR.

---

## 5. Практические моменты для firmware_test

### Инициализация — правильный порядок

```c
/* 1. Clock init — USB PLL должен быть поднят ДО USB init */
CLOCK_InitUsb1Pll(...);   /* 480 MHz USB PLL */
CLOCK_InitUsb1Pfd(...);

/* 2. PHY init */
USB_EhciPhyInit(CONTROLLER_ID, CLK_USRPH_24MHZ, NULL);

/* 3. Device stack init */
USB_DeviceInit(CONTROLLER_ID, USB_DeviceCallback, &handle);

/* 4. Регистрация CDC ACM класса */
USB_DeviceCdcAcmInit(...);

/* 5. Старт */
USB_DeviceRun(handle);
```

Если clock не инициализирован до USB — enumeration не пройдёт, хост увидит "USB device not recognized".

### Определение факта подключения хоста

Не надо проверять "есть ли питание на VBUS". Правильный способ — смотреть на **DTR флаг** из `SET_CONTROL_LINE_STATE`:

```c
static volatile bool s_host_connected = false;

/* внутри USB_DeviceCdcAcmCallback */
case kUSB_DeviceCdcEventSetControlLineState: {
    usb_device_cdc_acm_request_param_struct_t *p = param;
    /* бит 0 = DTR, бит 1 = RTS */
    s_host_connected = (p->setupValue & 0x01) != 0;
    break;
}

bool usb_cdc_is_connected(void) {
    return s_host_connected;
}
```

### Буферизация

NXP USB stack не буферизует — это твоя ответственность. Минимальная схема:

```
TX: кольцевой буфер → usb_cdc_write() кладёт туда данные
                     → USB task вычитывает и передаёт через USB_DeviceCdcAcmSend()
                     → по kUSB_DeviceCdcEventSendResponse — можно слать следующий чанк

RX: USB_DeviceCdcAcmRecv() регистрирует буфер для приёма
  → по kUSB_DeviceCdcEventRecvResponse — данные в буфере
  → приложение вычитывает до '\n' и парсит JSON
```

### Важно: двойная буферизация TX

`USB_DeviceCdcAcmSend()` принимает указатель на буфер и **не копирует данные**. Буфер должен жить до получения `kUSB_DeviceCdcEventSendResponse`. Типичная ошибка — передать указатель на локальную переменную.

```c
/* НЕПРАВИЛЬНО */
void send_something(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"type\":\"result\"}\n");
    USB_DeviceCdcAcmSend(handle, EP_BULK_IN, (uint8_t*)buf, strlen(buf));
    /* buf уходит из стека — UB! */
}

/* ПРАВИЛЬНО — статический или глобальный буфер */
static uint8_t s_tx_buf[512];
```

---

## 6. Схема эндпоинтов для дескрипторов

```
EP номер  Направление  Тип        Размер пакета  Назначение
────────  ───────────  ─────────  ─────────────  ──────────
EP0       IN + OUT     Control    64 байта        enumeration (автоматически)
EP1       IN           Interrupt  16 байт         CDC нотификации (DTR/RTS events)
EP2       IN           Bulk       64 байта (FS)   данные device → host
EP3       OUT          Bulk       64 байта (FS)   данные host → device
```

Номера EP назначаются в дескрипторах. NXP примеры используют именно эту схему для Full Speed CDC ACM.

---

## 7. Отладочные признаки проблем

| Симптом | Вероятная причина |
|---------|------------------|
| "USB device not recognized" на хосте | не инициализирован USB PLL / PHY |
| Устройство определяется, порт не появляется | ошибка в дескрипторах (класс, подкласс, протокол) |
| Порт появился, данные не идут | DTR не поднят (терминал не открыт) или ошибка TX буферизации |
| Данные обрываются / мусор | буфер TX освобождается до SendResponse |
| Работает раз через раз | нет re-submit RX буфера после RecvResponse |
| Зависает при переподключении | нет обработки kUSB_DeviceEventBusReset → не сбрасываются эндпоинты |

---

## 8. Ссылки

- `sdk/middleware/usb/` — исходники NXP USB stack
- `sdk/boards/evkbimxrt1050/usb_examples/usb_device_cdc_vcom/` — референсный пример
- `sdk/middleware/usb/device/class/usb_device_cdc_acm.c` — реализация класса
- `sdk/middleware/usb/include/usb_device_cdc_acm.h` — API класса
- USB 2.0 Specification — [usb.org](https://www.usb.org/document-library/usb-20-specification)
- USB CDC Specification (PSTN) — [usb.org](https://www.usb.org/document-library/class-definitions-communication-devices-12)