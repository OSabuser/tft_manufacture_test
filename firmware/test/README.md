# firmware_test

Bare-metal прошивка входного контроля платы **MIMXRT1052CVJ5B**.  
Загружается через USB ROM (SDP) в Flash. Запускается автономно при включении.

---

## Канал связи

Единственный транспорт: **USB CDC ACM** (USB1 / EHCI0, разъём J2).  
UART (MCU-Link VCOM) не используется — только в HIL ELF прошивках.

```bash
Хост (pytest / minicom / pyserial)
    └── USB CDC ACM (J2)
            └── firmware_test
                    └── cli.c → dispatch → обработчик команды
```

---

## Протокол

**JSON-lines**: каждая строка завершается `\n`.

| Направление | Формат |
|---|---|
| Запрос | `{"cmd":"NAME"}\n` |
| Успех | `{"ok":true,...}\n` |
| Ошибка | `{"ok":false,"error":"CODE"}\n` |

Ограничения:

- Максимальная длина строки: **128 байт** включая `\n` (`CLI_LINE_BUF_SIZE`)
- Терминатор: `\n` (LF, `0x0A`). `\r\n` (CR+LF) тоже принимается

---

## Команды

| Команда | Запрос | Успешный ответ | Описание |
|---|---|---|---|
| `PING` | `{"cmd":"PING"}` | `{"ok":true,"result":"PONG"}` | Проверка связи |
| `SDRAM_TEST` | `{"cmd":"SDRAM_TEST"}` | `{"ok":true,"time_ms":N}` | Быстрый тест SDRAM (≤100 мс) |
| `SDRAM_TEST_FULL` | `{"cmd":"SDRAM_TEST_FULL"}` | `{"ok":true,"time_ms":N}` | Полный тест SDRAM (~30–60 с) |

Коды ошибок:

| Код | Причина |
|---|---|
| `UNKNOWN_CMD` | Команда не найдена в таблице |
| `PARSE_ERR` | Не найдено поле `"cmd"` в JSON |
| `LINE_TOO_LONG` | Строка превысила `CLI_LINE_BUF_SIZE` |
| `SDRAM_FAIL` | Тест SDRAM выявил ошибку чтения/записи |

---

## Последовательность старта

```bash
board_hw_init()       тактирование, MPU, кэш, пины
bsp_tick_init()       SysTick 1 мс
bsp_led_init()        оба LED выключены
bsp_usb_cdc_init()    PHY + USB стек + NVIC

[ожидание хоста]      LED_HEARTBEAT мигает 200 мс
                      bsp_usb_cdc_is_ready() == true

LED_APP on
→ {"ok":true,"result":"READY"}    сигнал готовности хосту

[главный цикл]
    bsp_usb_cdc_poll()
    cli_process()
```

---

## Структура файлов

```bash
firmware/test/
├── CMakeLists.txt
├── README.md                   ← этот файл
└── src/
    ├── main.c                  # точка входа, init, main loop
    ├── cli.h                   # публичный API CLI
    ├── cli.c                   # буферизация RX, парсинг "cmd", dispatch
    └── tests/
        ├── test_sdram.c        # SDRAM_TEST / SDRAM_TEST_FULL  [TODO]
        ├── test_sdram.h
        ├── test_led.c          # LED_ON / LED_OFF              [TODO]
        ├── test_led.h
        ├── test_button.c       # BUTTON_READ                   [TODO]
        └── test_button.h
```

---

## Добавление новой команды

1. В `cli.c` объявить обработчик: `static void cmd_foo(void);`
2. Добавить строку в таблицу `k_cmds[]`: `{ "FOO", cmd_foo }`
3. Реализовать обработчик в секции `Command handlers`
4. Добавить тест в `tests/host/cli/test_cli.c`
5. Обновить таблицу команд в этом README

---

## Сборка и прошивка

```bash
# devcontainer
just build::build-firmware-test-debug
just build::hab-firmware-test-debug

# хост (плата в SDP-режиме: BOOT_MOD_1 → 3V3 → Reset)
just host::flash-test-debug

# вернуть в нормальный режим
# BOOT_MOD_1 → GND → Reset
```

---

## Тестирование

```bash
# Host unit-тесты CLI парсера (devcontainer, без железа)
just build::test-host                        # → test_cli

# HIL тест USB CDC (хост, требует подключённой платы)
just host::hil-usb-cdc

# HIL тест SDRAM (хост, firmware_test прошит в Flash) ????????
just host::hil-sdram                         # быстрый
just host::hil-sdram-full                    # полный (~60 с)
```

---

## Переменные окружения

```bash
HIL_USB_CDC_PORT=    # порт USB CDC таргета, например /dev/ttyACM1
HIL_USB_CDC_BAUD=    # 115200
HIL_USB_CDC_TIMEOUT= # 5.0
```

Шаблон: `.env.example`. Актуальные значения: `.env` (не коммитится).

---

## Известные ограничения

- **Один пакет за цикл**: `cli_process()` читает один USB bulk-пакет за вызов
  главного цикла. При потоке команд без задержки буфер может не успеть
  обработаться — добавить задержку на стороне хоста между командами (≥10 мс).
- **TX неблокирующий**: если TX занят — ответ теряется. Хост должен ждать
  предыдущий ответ перед отправкой следующей команды.
- **Нет персистентного состояния**: при сбросе питания все результаты теряются.
  pytest должен повторно подключаться и дожидаться `READY`.
