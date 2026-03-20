# tools/hil — HIL-тесты и утилиты отладки MIMXRT1052

Изолированное Python-окружение на базе [uv](https://docs.astral.sh/uv/) для:
- **HIL-тестов** (Hardware-in-the-Loop) — загрузка ELF в RAM и pytest через UART
- **GDB-сервера** для отладки из VSCode (pyocd gdbserver)
- **SWD-прошивки** через `flash_swd.py` (вызывается из `tools/host/`)

Запускается на **хост-машине** — не внутри devcontainer.

---

## Структура

```
tools/hil/
├── conftest.py       — pytest-фикстуры: загрузка ELF, открытие UART, ожидание READY
├── env_config.py     — конфигурация из переменных окружения (.env → just → pytest)
├── pyocd_utils.py    — утилиты pyOCD: FLEXRAM, ELF-загрузка, запуск из векторов
├── load_and_run.py   — CLI-обёртка: загрузить ELF в RAM и запустить вручную
├── test_uart.py      — HIL-тесты bsp_uart_host (PING/ECHO/BUF_SIZE)
├── pyproject.toml    — зависимости (pyocd, pyserial, pytest)
└── uv.lock           — lockfile (коммитить)
```

---

## Концепция HIL-тестов

Каждый HIL-тест — это пара:

```
tests/target/<name>/main.c      ← C-прошивка с текстовым CLI через UART
tools/hil/test_<name>.py        ← pytest-тесты, общаются с прошивкой по UART
```

pyOCD загружает `.elf` в RAM через MCU-Link (CMSIS-DAP). pytest общается с
прошивкой через MCU-Link VCOM (pyserial):

```
pytest → uart_cmd("PING\r\n") → MCU-Link VCOM → RT1052 → "PONG\r\n" → pytest
```

ELF загружается в **ITCM/DTCM** (не Flash) — быстро, не изнашивает Flash,
не требует HAB-образа.

---

## Предварительные требования

### 1. uv — один раз на машину

```bash
# macOS / Linux
curl -LsSf https://astral.sh/uv/install.sh | sh

# Windows
powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
```

### 2. Зависимости проекта — один раз

```bash
cd tools/hil
uv sync
```

### 3. Настроить `.env` в корне репозитория

```bash
HIL_VCOM_PORT=/dev/tty.usbmodemGUXFBWDJBWTGQ3   # macOS
# HIL_VCOM_PORT=/dev/ttyACM0                      # Linux
# HIL_VCOM_PORT=COM3                              # Windows
HIL_VCOM_BAUD=115200
HIL_READY_TIMEOUT=5.0
HIL_PYOCD_FREQUENCY=1000000
HIL_BUILD_DIR=build/target-debug
```

Найти порт MCU-Link VCOM:

```bash
just host::scan          # nxpdevscan — все NXP устройства
ls /dev/tty.usbmodem*    # macOS
ls /dev/ttyACM*          # Linux
```

---

## Запуск HIL-тестов

Сборка target-прошивок выполняется в devcontainer:

```bash
# devcontainer:
just build::build-hil    # → build/target-debug/tests/target/*/test_*.elf
```

Запуск тестов — на хосте:

```bash
just host::hil-run       # все HIL-тесты
just host::hil-smoke     # только smoke-тесты (быстро)
just host::hil-run-fast  # без перезагрузки ELF (прошивка уже запущена)
```

Или напрямую через pytest:

```bash
cd tools/hil

# Все тесты
uv run pytest -v

# Только smoke
uv run pytest -v -m smoke

# Конкретный файл
uv run pytest test_uart.py -v

# Без перезагрузки ELF (прошивка уже запущена)
uv run pytest -v --no-load

# Другой ELF
uv run pytest test_uart.py -v --elf /path/to/custom.elf

# Другой VCOM-порт
uv run pytest -v --vcom /dev/ttyACM1
```

---

## Конфигурация

Конфигурация читается в `env_config.py`. Приоритет: CLI-опции pytest > `os.environ` > defaults.

| Переменная окружения | CLI pytest | Default | Описание |
|---|---|---|---|
| `HIL_BUILD_DIR` | — | `build/target-debug` | Директория с target ELF-файлами |
| `HIL_VCOM_PORT` | `--vcom` | `/dev/ttyACM0` | UART-порт MCU-Link VCOM |
| `HIL_VCOM_BAUD` | — | `115200` | Скорость UART |
| `HIL_READY_TIMEOUT` | — | `5.0` | Таймаут ожидания `READY` от прошивки (сек) |
| `HIL_PYOCD_FREQUENCY` | — | `1000000` | Частота SWD для загрузки ELF в RAM |

При запуске через `just` все переменные из корневого `.env` автоматически
экспортируются в окружение (`set dotenv-load` + `set export`).

---

## Как работают фикстуры

### Цепочка зависимостей

```bash
test_ping()
  └── _setup (autouse, scope=function)
        ├── loaded_host_uart (scope=module)  ← грузит ELF в RAM
        └── uart (scope=module)              ← открывает VCOM, ждёт READY
              └── depends_on: loaded_host_uart
```

`scope=module` — ELF загружается один раз на весь файл с тестами, порт
открывается один раз. Все тесты внутри файла разделяют одно соединение.

### Порядок выполнения

```bash
1. loaded_<n>()
   ├── open_target()       → pyOCD: подключиться к MCU через SWD
   ├── flexram_init()      → настроить ITCM/DTCM/OCRAM
   ├── load_elf()          → записать PT_LOAD сегменты по адресам
   └── run_from_vectors()  → SP/PC из 0x00000000/0x00000004 → resume

2. uart()
   ├── serial.Serial.open()
   ├── while readline() != "READY": ...   ← ждём сигнал от прошивки
   └── yield ser

3. test_ping(), test_echo(), ...          ← тесты

4. uart teardown → ser.close()
```

### Почему `uart` зависит от `loaded_<n>`

```python
def uart(request, loaded_host_uart):  # ← явная зависимость в сигнатуре
```

Без этого pytest мог бы создать `uart` раньше чем ELF загружен — порт открылся
бы, но `READY` не пришёл. Явная зависимость гарантирует порядок.

---

## pyocd_utils — справочник

### `open_target(frequency)`

Контекстный менеджер, открывает pyOCD-сессию с первым найденным пробником.
Таргет — `cortex_m` (generic, без flash-алгоритма — для RAM-операций достаточно).

```python
with open_target(frequency=1_000_000) as target:
    flexram_init(target)
    load_elf(target, "test.elf")
    run_from_vectors(target)
```

### `flexram_init(target)`

Настраивает FLEXRAM через `IOMUXC_GPR16/GPR17`:

- 128 KB ITCM (0x00000000) — код
- 128 KB DTCM (0x20000000) — данные, стек
- 256 KB OCRAM (0x20200000) — буферы

### `load_elf(target, elf_path)`

Записывает все `PT_LOAD` сегменты ELF по физическим адресам (`p_paddr`).
Использует `pyelftools`.

### `run_from_vectors(target)`

Читает SP и PC из таблицы векторов (ITCM `0x00000000`/`0x00000004`),
выставляет регистры, вызывает `target.resume()`. Проверяет:

- SP в диапазоне DTCM `[0x20000000, 0x20040000]`
- PC в диапазоне ITCM `[0x00000400, 0x00020000]`

### `load_and_run(elf_path, frequency)`

Комбо-функция: open_target + flexram_init + load_elf + run_from_vectors.
Для CLI и одиночных скриптов.

---

## load_and_run.py — ручная загрузка ELF

Для ручной отладки без запуска тестов:

```bash
# Загрузить ELF в RAM и запустить
uv run python load_and_run.py build/target-debug/tests/target/host_uart/test_host_uart.elf

# Через just:
just host::hil-load
```

После этого можно подключиться к VCOM вручную:

```bash
just host::uart-monitor
```

---

## Протокол CLI в target-прошивках

Все HIL target-прошивки (`tests/target/<name>/main.c`) реализуют единый
текстовый CLI через `bsp_uart_host`:

- Прошивка отправляет `READY\r\n` пока хост не открыл порт
- Хост посылает команду строкой с `\r\n`
- Прошивка отвечает одной строкой с `\r\n`

Минимальные команды в каждой прошивке:

| Команда | Ответ | Назначение |
|---|---|---|
| `PING` | `PONG` | Проверка канала |
| `<неизвестная>` | `ERR_UNKNOWN` | Прошивка не зависает |

---

## Добавление нового HIL-теста

Подробный гайд — в `tests/HIL_CREATE_TEST.md`. Краткая схема:

```bash
1. tests/target/<name>/main.c          — C-прошивка с CLI
2. tests/target/<name>/CMakeLists.txt  — сборка с bsp_boot_ram
3. tests/target/CMakeLists.txt         — add_subdirectory(<name>)
4. CMakePresets.json                   — добавить test_<name> в target-debug-build
5. tools/hil/test_<name>.py            — pytest-тесты
6. tools/hil/conftest.py               — добавить loaded_<name> фикстуру
```

Фикстура для нового теста в `conftest.py`:

```python
@pytest.fixture(scope="module")
def loaded_<name>(request: pytest.FixtureRequest) -> None:
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/<name>/test_<name>.elf",
    )
```

---

## Совместимость с GDB-сервером

pyOCD используется для двух независимых задач:

| Задача | Команда | Таргет | Порт |
|---|---|---|---|
| HIL-тесты (RAM) | `pyocd` через `pyocd_utils` | `cortex_m` | — |
| GDB-сервер (отладка) | `pyocd gdbserver` | `mimxrt1050_quadspi` | 3333 |

**MCU-Link монопольный** — нельзя запускать HIL и GDB-сервер одновременно.
Перед `just host::hil-run` остановите GDB-сервер (`Ctrl+C`), и наоборот.

Разные таргеты намеренны: HIL не нужен flash-алгоритм (`cortex_m` достаточно),
GDB-сервер нужен полноценный таргет для корректного reset и SVD.
