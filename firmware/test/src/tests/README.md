# firmware_test — Тест-модули

> Расположение: `firmware/test/src/tests/README.md`
>
> Каждый тест-модуль реализует интерфейс `test_module_t` и регистрируется
> в реестре `test_runner.c`. Этот документ описывает что именно проверяет
> каждый тест, какие гарантии даёт, какие аппаратные инварианты должны
> соблюдаться, и каковы ограничения.

---

## Содержание

- [Как читать эту таблицу](#как-читать-эту-таблицу)
- [test_sdram — SDRAM 32 MB](#test_sdram--sdram-32-mb)
- [test_qspi — QSPI Flash 8 MB](#test_qspi--qspi-flash-8-mb) *(запланирован)*
- [test_usd — uSD SDIO](#test_usd--usd-sdio) *(запланирован)*
- [test_display — Display RGB888](#test_display--display-rgb888) *(запланирован)*
- [test_buttons — Test_But_1 / Test_But_2](#test_buttons--test_but_1--test_but_2) *(запланирован)*
- [test_can — CAN loopback](#test_can--can-loopback) *(запланирован)*
- [test_uart_ttl — UART TTL](#test_uart_ttl--uart-ttl) *(запланирован)*
- [test_uart_iso — UART ISO / RS_RX](#test_uart_iso--uart-iso--rs_rx) *(запланирован)*
- [test_opto — Opto-in EXT_IN1/IN2](#test_opto--opto-in-ext_in1in2) *(запланирован)*

---

## Как читать эту таблицу

**Critical** — при провале в `run_all` все последующие тесты получают `SKIP`.
Некритические тесты могут упасть без остановки прогона.

**HIL** — тест требует внешних сигналов от M5StampPLC.
Без стенда тест вернёт `SKIP` или `FAIL`.

**Тип confirm:**

- `pre_confirm` — test_runner ждёт JSON-ответа оператора до запуска `run()`.
- `в run()` — тест сам вызывает `test_runner_wait_confirm()` внутри.
- `prompt only` — отправляет `confirm_request` как UI-подсказку,
  ответ не ожидается (тест детектирует физическое событие сам).
- `—` — неинтерактивный тест.

---

## test_sdram — SDRAM 32 MB

**Файл:** `test_sdram.c`
**ID:** `sdram`
**Critical:** ✅ | **HIL:** ❌ | **Confirm:** —

### Аппаратный контекст

| Параметр | Значение |
|---|---|
| Чип | MT48LC16M16A2 |
| Объём | 32 MB |
| Шина данных | 16 бит |
| Интерфейс MCU | SEMC (0x402F0000) |
| Базовый адрес | 0x80000000 |
| Тестовый регион | 0x80200000 — 0x80A00000 |
| MPU | Region 8: Normal WB cacheable (BOARD_MPU_SDRAM=1) |

### Инварианты выполнения

- SEMC инициализирован DCD **до** `main()` — `bsp_sdram_init()` только верифицирует.
- MPU Region 8 (`Normal WB`) активен — кэш включён для SDRAM.
- MPU Region 9 (`Non-cacheable`, 0x81E00000, 2MB) активен — USB DMA изолирован.
- Тестовый регион не пересекается с `.data`/`.bss` прошивки (смещение +2 MB от базы).
- Тестовый регион не пересекается с non-cacheable регионом (граница 0x81E00000).
- Сброс кэша (`SCB_CleanDCache_by_Addr`) выполняется **после каждого** write-прохода —
  readback всегда из физической SDRAM, не из кэша.

### Что тестирует — четыре фазы

#### Фаза 1: Address bus (~1 мс)

Записывает уникальный байт в 24 позиции на степенях двойки
(2^0..2^23 от TEST_BASE), каждую с индивидуальным cache line flush.

**Покрытие:** все 24 адресных бита MT48LC16M16A2 (13 row + 9 col + 2 bank).

**Ловит:**

- Address aliasing — замыкание адресных линий SEMC.
- Неправильное подключение адресных линий к чипу.

**Не ловит:**

- Деградацию отдельных ячеек вне точек степеней двойки.

---

#### Фаза 2: Data bus (~1 с)

Walking ones (0x01, 0x02, ..., 0x80, 0x01, ...) и его инверсия
на регионе 64 KB.

**Покрытие:** все 8 бит шины данных.

**Ловит:**

- Stuck-at-0 и stuck-at-1 фолты на битах шины данных.
- Обрыв линии DATA между MCU и чипом.

**Не ловит:**

- Coupling между несмежными битами (для этого — фаза 3).

---

#### Фаза 3: Sequential integrity (~25 с)

Address pattern (`offset & 0xFF`) и его инверсия на регионе 2 MB,
два паттерна × два прохода (write → flush → verify).

**Покрытие:** 2 MB непрерывного адресного пространства.

**Ловит:**

- Coupling faults между соседними ячейками.
- Деградированные ячейки в тестируемом регионе.
- Частичный address aliasing внутри 2 MB.

**Не ловит:**

- Деградацию ячеек вне тестируемых 2 MB (всего 32 MB в чипе).

---

#### Фаза 4: Retention (~2 с)

Address pattern на 256 KB: запись → `flush_dcache` → ожидание 200 мс → верификация.

200 мс ≈ 3 полных refresh-периода MT48LC16M16A2 (период = 64 мс).

**Покрытие:** 256 KB с задержкой на несколько refresh-циклов.

**Ловит:**

- Refresh timing failures — ячейки теряют данные между refresh-циклами.
- Деградацию конденсаторов ячеек памяти (capacitor leakage).

**Не ловит:**

- Retention failures при температурных крайностях.

---

### Гарантии теста при PASS

- Все 24 адресных бита работают независимо без aliasing.
- Все 8 бит шины данных переключаются корректно.
- 2 MB последовательных ячеек не имеют coupling faults.
- 256 KB удерживают данные минимум через 3 refresh-цикла.
- SEMC контроллер инициализирован и отвечает.

### Интерпретация FAIL

`detail` содержит: `addr=0xXXXXXXXX exp=0xXX got=0xXX`

| Диапазон addr | Вероятная фаза | Диагноз |
|---|---|---|
| `0x80200000` — `0x80A00000` (степени двойки) | Фаза 1 | Address aliasing |
| `0x80200000` — `0x80210000` | Фаза 2 | Stuck-at на шине данных |
| `0x80200000` — `0x80400000` | Фаза 3 | Coupling или деградация ячейки |
| `0x80200000` — `0x80240000` | Фаза 4 | Refresh timing failure |

**Анализ `exp` XOR `got`:** биты где `(exp ^ got) != 0` — сбойные линии шины данных.

### Ограничения

- Не покрывает все 32 MB (только 2 MB для sequential).
- Не проверяет retention при нагреве или низком напряжении питания.
- Не является заменой полного March C− алгоритма.
- Во время теста (~28 с) USB CDC занят, `ping` не отвечает.

### Типичное время выполнения

| Фаза | ~Время |
|---|---|
| Address bus | < 1 мс |
| Data bus | ~1 с |
| Sequential | ~25 с |
| Retention | ~2 с |
| **Итого** | **~28 с** |

---

## test_qspi — QSPI Flash 8 MB

**Файл:** `test_qspi.c` *(не реализован — Этап 3)*
**ID:** `qspi`
**Critical:** ✅ | **HIL:** ❌ | **Confirm:** —

### Аппаратный контекст

| Параметр | Значение |
|---|---|
| Чип | W25Q64FVSSIG |
| Объём | 8 MB |
| Интерфейс | FlexSPI |
| BSP | `bsp_qspi` |

### Что будет тестировать

- JEDEC ID верификация: manufacturer=`0xEF`, device=`0x4017`.
- Erase последнего сектора (4 KB, не затрагивает прошивку).
- Write 256 байт → Read → Compare.

### Гарантии при PASS

- FlexSPI контроллер инициализирован.
- Чип отвечает с ожидаемым JEDEC ID.
- Базовые операции чтения/записи/стирания работают.

---

## test_usd — uSD SDIO

**Файл:** `test_usd.c` *(не реализован — Этап 4)*
**ID:** `usd`
**Critical:** ❌ | **HIL:** ❌ | **Confirm:** pre_confirm

### Аппаратный контекст

| Параметр | Значение |
|---|---|
| Интерфейс | USDHC |
| Файловая система | FatFS (SDK middleware) |
| BSP | `bsp_usd` |
| Pre-confirm prompt | "Вставьте карту microSD и нажмите OK" |

### Что будет тестировать

- Mount FAT — карта вставлена и файловая система читаема.
- Write тестового файла → Read → Compare (деструктивно для одного файла).
- Unmount.

### Поведение без карты

Оператор отказался от pre_confirm или таймаут 30 с → `TEST_STATUS_SKIP`.

---

## test_display — Display RGB888

**Файл:** `test_display.c` *(не реализован — Этап 5)*
**ID:** `display`
**Critical:** ❌ | **HIL:** ❌ | **Confirm:** в run()

### Что будет тестировать

Четыре шага с визуальным подтверждением оператора:

| Шаг | ID confirm | Промпт | Таймаут |
|---|---|---|---|
| Красный | `display_red` | "Экран залит красным?" | 15 с |
| Зелёный | `display_green` | "Экран залит зелёным?" | 15 с |
| Синий | `display_blue` | "Экран залит синим?" | 15 с |
| Белый | `display_white` | "Экран залит белым?" | 15 с |

Итог = AND всех четырёх подтверждений.

### Гарантии при PASS

- RGB888 интерфейс выводит каждый цвет независимо.
- Подсветка PWM активна (оператор видит экран).

### Интерпретация FAIL

`detail` содержит ID шага, который не был подтверждён: `"display_blue not confirmed"`.
Это указывает на конкретный цветовой канал с дефектом.

---

## test_buttons — Test_But_1 / Test_But_2

**Файл:** `test_buttons.c` *(не реализован — Этап 5)*
**ID:** `buttons`
**Critical:** ❌ | **HIL:** ❌ | **Confirm:** prompt only

### Аппаратный контекст

| Кнопка | Пин MCU | GPIO |
|---|---|---|
| Test_But_1 | GPIO_B1_14 | GPIO2[30] |
| Test_But_2 | GPIO_B1_15 | GPIO2[31] |

### Что будет тестировать

Два шага. Таргет отправляет `confirm_request` как инструкцию оператору
и детектирует нажатие через `bsp_button` — JSON confirm не нужен.

| Шаг | ID | Промпт | Таймаут |
|---|---|---|---|
| Кнопка 1 | `btn1_press` | "Нажмите кнопку Test_But_1" | 10 с |
| Кнопка 2 | `btn2_press` | "Нажмите кнопку Test_But_2" | 10 с |

### Гарантии при PASS

- Оба GPIO входа корректно регистрируют нажатие.
- `bsp_button` debounce логика работает.

---

## test_can — CAN loopback

**Файл:** `test_can.c` *(не реализован — Этап 6)*
**ID:** `can`
**Critical:** ❌ | **HIL:** ✅ | **Confirm:** —

### Аппаратный контекст

| Параметр | Значение |
|---|---|
| BSP | `bsp_can` ✅ |
| M5 | CAN интерфейсная плата → шина платы |

### Что будет тестировать

M5StampPLC отправляет CAN фрейм → плата принимает → сравниваем ID и данные.

### Гарантии при PASS

- CAN контроллер и трансивер работают.
- Принятый фрейм совпадает с отправленным по ID и payload.

---

## test_uart_ttl — UART TTL

**Файл:** `test_uart_ttl.c` *(не реализован — Этап 6)*
**ID:** `uart_ttl`
**Critical:** ❌ | **HIL:** ✅ | **Confirm:** —

### Аппаратный контекст

| Параметр | Значение |
|---|---|
| BSP | `bsp_uart_host` ✅ |
| M5 | UART ↔ UART TTL платы |

### Что будет тестировать

M5 отправляет пакет → плата получает → echo обратно → M5 верифицирует.

---

## test_uart_iso — UART ISO / RS_RX

**Файл:** `test_uart_iso.c` *(не реализован — Этап 6)*
**ID:** `uart_iso`
**Critical:** ❌ | **HIL:** ✅ | **Confirm:** —

### Аппаратный контекст

| Параметр | Значение |
|---|---|
| BSP | `bsp_opto` (rs_as_gpio=true) ✅ |
| Пин MCU | GPIO_AD_B1_07 / GPIO1[23] |
| Оптопара | PS2801-4 (неинвертирующая, active-HIGH) |
| M5 | RLY2 → RS_RX |

### Что будет тестировать

M5 RLY2 активирует оптовход RS_RX → плата детектирует через `bsp_opto`.
M5 RLY2 деактивирует → плата детектирует inactive.

### Инварианты

- `bsp_opto` инициализирован с `rs_as_gpio=true`.
- Пин GPIO_AD_B1_07 переведён в GPIO INPUT (не LPUART3_RX).

---

## test_opto — Opto-in EXT_IN1/IN2

**Файл:** `test_opto.c` *(не реализован — Этап 6)*
**ID:** `opto`
**Critical:** ❌ | **HIL:** ✅ | **Confirm:** —

### Аппаратный контекст

| Канал | Пин MCU | GPIO | Оптопара | M5 |
|---|---|---|---|---|
| EXT_IN1 | GPIO_AD_B1_06 | GPIO1[22] | PS2801-4 | RLY3 |
| EXT_IN2 | GPIO_AD_B1_05 | GPIO1[21] | PS2801-4 | RLY4 |

### Что будет тестировать

Каждый канал независимо: M5 активирует реле → плата детектирует ACTIVE →
M5 деактивирует → плата детектирует INACTIVE.

### Гарантии при PASS

- Оба оптоизолированных входа корректно детектируют HIGH/LOW.
- Debounce логика `bsp_opto` (MODE_LEVEL) работает корректно.

---

## Порядок в реестре `test_runner.c`

```c
static const test_module_t *const k_registry[] = {
    &K_TEST_SDRAM,       /* critical — первым */
    &K_TEST_QSPI,        /* critical */
    &K_TEST_USD,         /* non-critical, interactive, pre_confirm */
    &K_TEST_DISPLAY,     /* non-critical, interactive, confirm в run() */
    &K_TEST_BUTTONS,     /* non-critical, interactive, prompt only */
    &K_TEST_CAN,         /* non-critical, HIL */
    &K_TEST_UART_TTL,    /* non-critical, HIL */
    &K_TEST_UART_ISO,    /* non-critical, HIL */
    &K_TEST_OPTO,        /* non-critical, HIL */
};
```

Critical тесты идут первыми — при их провале HIL и интерактивные тесты
пропускаются автоматически, экономя время диагностики.
