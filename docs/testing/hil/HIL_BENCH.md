# HIL Стенд

Описание аппаратного стенда, подключений, инструментов и их роли в HIL-тестировании.

---

## Оборудование

| Устройство        | Роль                                                        |
|-------------------|-------------------------------------------------------------|
| MIMXRT1052CVJ5B   | Таргет — плата под тестом                                   |
| M5Stack StamPLC   | Промежуточная платформа стенда: питание таргета + сигналы   |
| MCU-Link (CMSIS-DAP) | SWD-probe + VCOM (pyOCD загружает ELF, pytest читает UART) |

---

## Подключение

### Питание таргета

| M5 реле | Куда             | Назначение          |
|---------|------------------|---------------------|
| RLY1    | VIN таргета      | Управление питанием |

Питание включается и выключается автоматически фикстурой `m5` в `conftest.py`:

- `agent.power(True)` в setup — до загрузки ELF через pyOCD
- `agent.power(False)` в teardown — после завершения всех тестов модуля

### Оптоизолированные входы

Таргет: оптопары PS2801-4, **неинвертирующие** (active-HIGH).
M5: реле AW9523B через ULN2003A, нормально разомкнутые (NO).

| M5 реле | Сигнал таргета | BSP канал          | MCU пин       | GPIO      |
|---------|----------------|--------------------|---------------|-----------|
| RLY2    | RS_RX          | `BSP_OPTO_CH_RS`   | GPIO_AD_B1_07 | GPIO1[23] |
| RLY3    | EXT_IN1        | `BSP_OPTO_CH_IN1`  | GPIO_AD_B1_06 | GPIO1[22] |
| RLY4    | EXT_IN2        | `BSP_OPTO_CH_IN2`  | GPIO_AD_B1_05 | GPIO1[21] |

**Логика сигнала:**

- Реле разомкнуто → нет тока через оптопару → пин LOW → `BSP_OPTO_STATE_INACTIVE`
- Реле замкнуто → ток через оптопару → пин HIGH → `BSP_OPTO_STATE_ACTIVE`

Маппинг зафиксирован в `tools/hil/m5/agent.py`:

```python
_OPTO_TO_RELAY = {1: 3, 2: 4, 3: 2}
#   ch1 (EXT_IN1 / BSP_OPTO_CH_IN1) → RLY3
#   ch2 (EXT_IN2 / BSP_OPTO_CH_IN2) → RLY4
#   ch3 (RS_RX   / BSP_OPTO_CH_RS ) → RLY2
```

> **При изменении физической разводки** — обновить `_OPTO_TO_RELAY` в `agent.py`
> и задеплоить: `just host::m5-deploy`.

### Порты и переменные окружения

| Переменная           | Значение по умолчанию | Назначение                |
|----------------------|-----------------------|---------------------------|
| `HIL_VCOM_PORT`      | `/dev/ttyACM0`        | MCU-Link VCOM (UART CLI)  |
| `HIL_VCOM_BAUD`      | `115200`              | Скорость UART CLI         |
| `HIL_M5_PORT`        | `/dev/ttyACM1`        | M5StampPLC USB CDC        |
| `HIL_M5_BAUD`        | `115200`              | Скорость M5 агента        |
| `HIL_BUILD_DIR`      | `build/target-debug`  | Путь к собранным ELF      |

На macOS порты выглядят как `/dev/cu.usbmodem*`. Задаются в `.env` в корне репозитория.

---

## Инструменты стенда

### M5Stack StamPLC — `tools/hil/m5/`

| Файл         | Назначение                                                     |
|--------------|----------------------------------------------------------------|
| `agent.py`   | MicroPython агент на M5. Принимает JSON-команды через USB CDC, подает сигналы на таргет |
| `cli.py`     | Интерактивный CLI для ручного тестирования агента              |
| `power.py`   | Скрипт управления питанием таргета (RLY1) из командной строки  |

**Протокол агента:** JSON-lines через USB CDC (115200 бод).

```bash
хост → M5:  {"cmd": "opto_set", "ch": 1, "state": true}\r\n
M5 → хост:  {"ok": true, "opto_ch": 1, "relay": 3, "state": true}\r\n
```

**Доступные команды агента:**

| Команда        | Параметры                          | Действие                                          |
|----------------|------------------------------------|---------------------------------------------------|
| `ping`         | —                                  | Проверка связи                                    |
| `info`         | —                                  | Версия, состояние CAN и AW9523, статус реле       |
| `power`        | `state: bool`                      | RLY1 — питание таргета                            |
| `relay_set`    | `ch: 1-4, state: bool`             | Прямое управление реле                            |
| `relay_get`    | `ch: 1-4`                          | Прочитать текущее состояние реле                  |
| `relay_all_off`| —                                  | Выключить все реле                                |
| `opto_set`     | `ch: 1-3, state: bool`             | Управление оптоканалом таргета (через маппинг)    |
| `opto_all_off` | —                                  | Выключить все оптоканалы                          |
| `input_read`   | `ch: 1-8`                          | Прочитать вход стенда SYS_IN (оптопара на M5)     |
| `can_send`     | `id: int, data: list[int], ext: bool=false` | Отправить CAN-фрейм с шины M5           |
| `can_recv`     | `timeout_ms: int=500`              | Принять CAN-фрейм на M5 (ошибка при таймауте)    |

**Деплой агента на M5:**

```bash
just host::m5-deploy
```

> ⚠️ После изменения `agent.py` обязательно задеплоить перед запуском тестов.

### MCU-Link — загрузка ELF и UART CLI

MCU-Link выполняет две роли одновременно:

**SWD (pyOCD)** — загружает `.elf` в RAM таргета перед каждой тест-сессией:

```bash
pyOCD → MCU-Link SWD → MIMXRT1052
  halt → FLEXRAM init → load ELF → run_from_vectors
```

**VCOM (pyserial)** — текстовый CLI для общения с прошивкой во время тестов:

```bash
pytest → pyserial → MCU-Link VCOM → LPUART1 → прошивка таргета
  uart_cmd("PING") → "PONG"
  uart_cmd("OPTO_READ 1") → "ACTIVE"
```

> ⚠️ MCU-Link занимает SWD монопольно. GDB-сервер (`just host::debug-server`)
> и загрузка ELF через pyOCD не могут работать одновременно.

### pytest + conftest.py — оркестрация

`tools/hil/conftest.py` содержит всю логику подготовки стенда:

```bash
фикстура m5 (scope=module)
  ├── подключиться к M5 агенту
  ├── agent.power(True)          ← RLY1: питание таргета ON
  ├── sleep(1.0 с)               ← ждём POR + стабилизацию
  └── agent.opto_all_off()       ← все сигнальные реле выключены

фикстура loaded_<n> (scope=module, зависит от m5)
  └── pyOCD: FLEXRAM → load ELF → run_from_vectors

фикстура uart_<n> (scope=module, зависит от loaded_<n>)
  └── открыть VCOM, ждать "READY\r\n" от прошивки

тесты (scope=function)
  └── uart_cmd() / m5.opto_set() / assert

teardown
  └── agent.opto_all_off() → agent.power(False) → ser.close()
```

---

## Just-рецепты стенда

```bash
# Деплой агента на M5 (после изменений agent.py)
just host::m5-deploy

# Проверить что M5 видна в системе
just host::m5-scan

# Открыть REPL на M5 для ручной отладки
just host::m5-repl

# Интерактивный CLI для ручного тестирования агента
just host::m5-cli

# Включить/выключить питание таргета вручную
just host::m5-power on
just host::m5-power off

# UART монитор — смотреть что шлёт прошивка
just host::uart-monitor
```

### HIL-тесты

```bash
# Запустить все автоматические тесты (без оператора)
just host::hil-run

# Запустить все интерактивные тесты (требуют оператора)
just host::hil-run-interactive

# Конкретные тесты
just host::hil-uart
just host::hil-opto
just host::hil-can
just host::hil-button    # интерактивный — нажатие кнопок оператором
```

---

## Добавление нового сигнала стенда

1. Физически подключить сигнал к свободному реле M5StampPLC.
2. Добавить маппинг в `agent.py` (по аналогии с `_OPTO_TO_RELAY`).
3. Добавить команду в `_dispatch()` в `agent.py`.
4. Добавить метод в класс `M5Agent` в `conftest.py`.
5. Задеплоить: `just host::m5-deploy`.
6. Обновить таблицу подключений выше.
