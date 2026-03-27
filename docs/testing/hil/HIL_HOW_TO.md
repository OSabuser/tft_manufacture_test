# Как проводить HIL-тесты

Пошаговый гайд для разработчика. Описывает полный цикл от сборки прошивки
до зелёного прогона тестов.

Детали оборудования и подключений — в [HIL_BENCH.md](HIL_BENCH.md).
Как добавить новый тест — в [HIL_CREATE_TEST.md](HIL_CREATE_TEST.md).

---

## Шаг 1 — Подготовка окружения (один раз)

### 1.1 Установить зависимости хоста

```bash
just host::setup-tools
```

Устанавливает Python-зависимости из `tools/hil/uv.lock` (pyOCD, pyserial, pytest
и др.). Повторный запуск — no-op если `uv.lock` не изменился.

### 1.2 Настроить `.env`

Скопировать `.env.example` в `.env` в корне репозитория и заполнить порты:

```bash
HIL_VCOM_PORT=/dev/cu.usbmodemXXX    # MCU-Link VCOM
HIL_M5_PORT=/dev/cu.usbmodemYYY      # M5StampPLC
HIL_BUILD_DIR=build/target-debug      # путь к собранным ELF (обычно не менять)
```

Как найти нужные порты:

```bash
just host::m5-scan   # покажет M5StampPLC
ls /dev/cu.usbmodem* # macOS: все USB CDC устройства
ls /dev/ttyACM*      # Linux: все USB CDC устройства
```

### 1.3 Задеплоить агент на M5StampPLC

```bash
just host::m5-deploy
```

Копирует `tools/hil/m5/agent.py` на M5 как `/main.py`. Агент запускается
автоматически при каждом включении M5.

> ⚠️ Повторять после **каждого изменения** `agent.py`. Иначе на M5 работает
> старая версия и тесты будут падать с неочевидными ошибками.

Проверить что агент работает:

```bash
just host::m5-cli    # интерактивный CLI для ручной отправки команд агенту
```

---

## Шаг 2 — Физическое подключение стенда

Убедиться что:

1. **MCU-Link** подключён к таргету по SWD и к хосту по USB.
2. **M5StampPLC** подключён к хосту по USB.
3. **RLY1** M5 подключён к VIN таргета — питание управляется программно.
4. Сигнальные реле подключены согласно таблице в [HIL_BENCH.md](HIL_BENCH.md).

Быстрая проверка стенда — включить питание таргета вручную и убедиться что
MCU-Link его видит:

```bash
just host::m5-power on

# В другом терминале — pyOCD должен найти пробник:
uv run --directory tools/hil pyocd list

just host::m5-power off
```

---

## Шаг 3 — Сборка HIL-прошивок (в devcontainer)

HIL-прошивки компилируются под ARM и собираются **внутри devcontainer**,
потому что там есть `arm-none-eabi-gcc` и весь SDK.

```bash
# Открыть проект в VSCode → Reopen in Container
# Затем внутри контейнера:
just build::build-hil
```

Что происходит: `cmake --build --preset target-debug-build` собирает все таргеты
из `tests/target/*/` и кладёт `.elf` в `build/target-debug/tests/target/`.

Проверить результат:

```bash
ls build/target-debug/tests/target/
# host_uart/test_host_uart.elf
# hil_opto/test_hil_opto.elf
# ...
```

> Пересобирать нужно только при изменении C-кода прошивок (`tests/target/*/main.c`
> или BSP). Изменения в Python-тестах (`tools/hil/test_*.py`) сборки не требуют.

---

## Шаг 4 — Запуск тестов (на хосте, вне контейнера)

### Все HIL-тесты

```bash
just host::hil-run
```

pytest обходит все `test_*.py` в `tools/hil/`, запускает их по очереди.
Каждый файл — своя загрузка ELF, свой UART-сеанс, MCU перезагружается между файлами.

### Конкретный тест

```bash
just host::hil-uart    # только test_uart.py
just host::hil-opto    # только test_opto.py
```

### Один тест-кейс (для отладки)

```bash
uv run --directory tools/hil pytest test_opto.py::TestOptoConnectivity::test_target_ping -v
```

### Без перезагрузки ELF (если прошивка уже запущена)

```bash
uv run --directory tools/hil pytest test_opto.py -v --no-load
```

Удобно при отладке тестов когда прошивка уже в RAM и не нужно каждый раз
ждать загрузки через pyOCD.

---

## Шаг 5 — Интерпретация результатов

### Зелёный прогон

```
18 passed in 13.4s
```

### Типичные ошибки и их причины

**`TimeoutError: Прошивка не отправила READY`**

Прошивка не запустилась. Возможные причины:
- ELF не пересобран после изменений — `just build::build-hil`
- MCU не получает питание — `just host::m5-power on`, проверить RLY1
- Неверный порт в `.env` — `HIL_VCOM_PORT`
- MCU-Link занят GDB-сервером — закрыть `just host::debug-server`

**`M5 agent не отвечает`**

- Агент не задеплоен — `just host::m5-deploy`
- M5 завис — отключить и подключить USB, повторить деплой
- Неверный порт — `HIL_M5_PORT` в `.env`

**Тест читает INACTIVE вместо ACTIVE (или наоборот)**

- Провод подключён не к тому реле — сверить [HIL_BENCH.md](HIL_BENCH.md)
- `agent.py` изменился но не задеплоен — `just host::m5-deploy`
- Слишком маленький `SETTLE_S` — дебаунс прошивки не успел отработать

**`pyOCD: No connected probes`**

- MCU-Link не подключён или не виден — проверить USB
- На Linux — нет udev-правил: `just host::setup-udev`

---

## Ручная отладка

Если тест падает и непонятно почему — загрузить ELF вручную и пообщаться
с прошивкой напрямую:

```bash
# Загрузить ELF без запуска тестов
uv run --directory tools/hil python load_and_run.py \
    build/target-debug/tests/target/hil_opto/test_hil_opto.elf

# В другом терминале — открыть UART монитор
just host::uart-monitor
# Теперь можно вводить команды вручную: PING, OPTO_READ 1, ...
```

Управлять стендом вручную через M5:

```bash
just host::m5-cli
# {"cmd": "opto_set", "ch": 1, "state": true}
# {"ok": true, ...}
```
