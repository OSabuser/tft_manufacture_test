# Pytest-фикстуры для Embedded: памятка

Памятка по pytest-фикстурам применительно к HIL-тестированию embedded-систем.
Все примеры — из реального проекта MIMXRT1052 (pyOCD + pyserial + M5StampPLC).

---

## 1. Что такое фикстура

Фикстура — это функция с декоратором `@pytest.fixture`, которая **готовит ресурс**
(порт, соединение, состояние железа) перед тестом и **убирает за собой** после.
pytest сам разруливает порядок вызовов, зависимости и время жизни.

```python
@pytest.fixture
def uart(request):
    ser = serial.Serial(port="/dev/ttyACM0", baudrate=115200, timeout=2.0)
    yield ser          # ← тест получает ser
    ser.close()        # ← teardown: выполнится после теста
```

---

## 2. `yield` vs `return`

| Конструкция | Setup | Teardown | Когда использовать |
|-------------|-------|----------|--------------------|
| `return`    | ✅    | ❌       | Ресурс не требует очистки (загрузка ELF) |
| `yield`     | ✅    | ✅       | Ресурс нужно освободить (порт, реле, питание) |

```python
# return — teardown не нужен
@pytest.fixture(scope="module")
def loaded_host_uart(request):
    _load_elf(request, Path("...test_host_uart.elf"))
    # ELF загружен, очищать нечего — return None неявно

# yield — teardown нужен
@pytest.fixture(scope="module")
def m5(request):
    agent = M5Agent(port=cfg.M5_PORT)
    agent.power(True)                  # setup: питание ON
    agent.opto_all_off()
    time.sleep(1.0)
    yield agent                        # тесты работают с agent
    agent.opto_all_off()               # teardown: сброс сигналов
    agent.power(False)                 # teardown: питание OFF
```

**Важно:** код после `yield` выполняется **всегда** — даже если тест упал с исключением.
Это гарантирует что реле будут выключены, порт закрыт, питание снято.

---

## 3. Scope — время жизни фикстуры

Scope определяет **как долго живёт** экземпляр фикстуры.

| Scope | Создаётся | Уничтожается | Типичное применение в HIL |
|-------|-----------|--------------|---------------------------|
| `function` | Перед каждым `test_*()` | После каждого `test_*()` | Сброс состояния стенда |
| `class` | Перед первым тестом класса | После последнего теста класса | Группа связанных тестов |
| `module` | Перед первым тестом файла | После последнего теста файла | **Загрузка ELF, открытие UART** |
| `session` | Один раз на весь pytest-запуск | В самом конце | Подключение к M5, глобальный setup |

### Почему для HIL основной scope — `module`

Каждый тест-файл (`test_uart.py`, `test_opto.py`) работает со **своей прошивкой**.
Загружать ELF перед каждой `test_*()` — слишком дорого (~2 с на загрузку через pyOCD).
`scope="module"` означает: загрузил один раз, прогнал все тесты файла, закрыл.

```python
@pytest.fixture(scope="module")
def loaded_host_uart(request):
    _load_elf(request, Path(cfg.BUILD_DIR) / "tests/target/host_uart/test_host_uart.elf")

@pytest.fixture(scope="module")
def uart(request, loaded_host_uart):   # зависит от loaded_host_uart
    ser = _open_uart_and_wait_ready(request)
    yield ser
    ser.close()
```

### Правило совместимости scope

Фикстура **не может зависеть** от фикстуры с более коротким scope:

```python
# ❌ ОШИБКА: module-фикстура зависит от function-фикстуры
@pytest.fixture(scope="function")
def short_lived():
    return 42

@pytest.fixture(scope="module")
def long_lived(short_lived):    # ScopeMismatch!
    return short_lived + 1
```

Допустимые зависимости: `session → module → class → function` (от длинного к короткому).

---

## 4. `autouse=True`

Фикстура с `autouse=True` применяется **автоматически** ко всем тестам в своей области
видимости — без явного указания в сигнатуре теста.

### В классе — применяется к тестам класса

```python
class TestOptoConnectivity:

    @pytest.fixture(autouse=True)
    def _setup(self, uart_hil_opto, m5):
        """Каждый тест в классе автоматически получает uart и m5."""
        self.ser = uart_hil_opto
        self.m5 = m5
        self.m5.opto_all_off()       # сброс перед каждым тестом
        time.sleep(0.15)

    def test_ping(self):
        assert uart_cmd(self.ser, "PING") == "PONG"

    def test_opto_ch1(self):
        self.m5.opto_set(1, True)
        time.sleep(0.15)
        assert uart_cmd(self.ser, "OPTO_READ 1") == "ACTIVE"
```

### В `conftest.py` — применяется ко всем тестам директории

```python
# conftest.py
@pytest.fixture(autouse=True, scope="function")
def flush_uart_buffer(uart):
    """Сбросить RX-буфер перед каждым тестом."""
    uart.reset_input_buffer()
```

**Правило:** `autouse` в `conftest.py` действует на **все** тесты в директории
и поддиректориях. Используется осторожно — можно случайно затронуть тесты,
которым эта фикстура не нужна.

---

## 5. Зависимости между фикстурами

Зависимость объявляется **в сигнатуре** фикстуры. pytest строит `DAG` и гарантирует
порядок создания.

```python
@pytest.fixture(scope="module")
def m5():
    agent = M5Agent(...)
    agent.power(True)
    yield agent
    agent.power(False)

@pytest.fixture(scope="module")
def loaded_hil_opto(request, m5):        # ← зависит от m5
    """m5 создастся РАНЬШЕ, питание будет ON до загрузки ELF."""
    _load_elf(request, Path("...test_hil_opto.elf"))

@pytest.fixture(scope="module")
def uart_hil_opto(request, loaded_hil_opto):  # ← зависит от loaded
    ser = _open_uart_and_wait_ready(request)
    yield ser
    ser.close()
```

### Цепочка в нашем проекте

```bash
test_opto_ch1()
  └── _setup (function, autouse)
        ├── uart_hil_opto (module)
        │     └── loaded_hil_opto (module)
        │           └── m5 (module)          ← питание ON
        └── m5 (module)                      ← тот же экземпляр
```

**Ключевой момент:** `loaded_hil_opto` зависит от `m5` — это гарантирует что
питание таргета включено **до** того как pyOCD попытается подключиться по SWD.
Без этой зависимости pytest мог бы создать `loaded_hil_opto` раньше `m5`,
и pyOCD не нашёл бы MCU.

---

## 6. `conftest.py` — автоматический импорт фикстур

`conftest.py` — специальный файл, который pytest подхватывает **без импорта**.
Все фикстуры из него доступны тестам в той же директории и ниже.

```bash
tools/hil/
├── conftest.py          ← фикстуры: _load_elf, uart, m5, loaded_*
├── test_uart.py         ← видит всё из conftest.py
├── test_opto.py         ← видит всё из conftest.py
└── m5/
    └── conftest.py      ← (если бы был) виден только в m5/
```

### Что живёт в `conftest.py` проекта

| Фикстура / функция | Scope | Назначение |
|---------------------|-------|------------|
| `_load_elf()` | вспомогательная | pyOCD: halt → FLEXRAM → load ELF → run |
| `_uart_context()` | контекстный менеджер | Открыть VCOM, дождаться `READY\r\n`, гарантировать `close()` |
| `_make_uart_fixture()` | фабрика | Генерирует `uart_*` фикстуры из `_UART_FIXTURE_MAP` |
| `_UART_FIXTURE_MAP` | словарь | Связь `uart_<n>` → `loaded_<n>` для всех тестов |
| `loaded_host_uart` | module | Загрузить `test_host_uart.elf` |
| `loaded_hil_opto` | module | Загрузить `test_hil_opto.elf`, зависит от `m5` |
| `uart` / `uart_opto` / ... | module | Создаются автоматически через `_UART_FIXTURE_MAP` |
| `usb_cdc_port` | module | USB CDC порт таргета, использует `cfg.TARGET_VCOM_*` |
| `m5` | module | Подключиться к M5, включить питание |
| `uart_cmd()` | обычная функция | Отправить команду, прочитать ответ |

---

## 7. Teardown — порядок уничтожения

Teardown выполняется в **обратном** порядке создания (LIFO):

```bash
Создание:          m5 → loaded_hil_opto → uart_hil_opto → _setup
                   ─────────────────────────────────────────────→

Уничтожение:       _setup → uart_hil_opto → loaded_hil_opto → m5
                   ←─────────────────────────────────────────────
```

Это значит:

1. Сначала закроется UART (`ser.close()`)
2. Потом (если есть teardown у loaded) — ничего
3. Последним — M5 выключит питание (`agent.power(False)`)

**Порядок гарантирован.** Питание не выключится раньше чем закроется порт.

---

## 8. Несколько тест-файлов — изоляция

При запуске нескольких файлов каждый получает **свой** набор module-фикстур:

```bash
pytest test_uart.py test_opto.py
```

```bash
test_uart.py                      test_opto.py
─────────────────────────         ─────────────────────────
loaded_host_uart  ← создаётся    m5             ← создаётся
uart              ← создаётся    loaded_hil_opto
  test_ping                      uart_hil_opto  ← создаётся
  test_echo                        test_ping
  test_buf_size                    test_opto_ch1
uart.close()      ← teardown      test_opto_ch2
                                 uart.close()   ← teardown
                                 m5.power(False) ← teardown
```

MCU перезагружается между файлами — у каждого теста своя прошивка.

---

## 9. Паттерны для Embedded HIL

### 9.1. Загрузка ELF (без teardown)

```python
@pytest.fixture(scope="module")
def loaded_<name>(request, m5):     # m5 — только если нужно питание
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/<name>/test_<name>.elf",
    )
```

Teardown не нужен — MCU будет перезагружен при следующей загрузке ELF.

### 9.2. UART-сессия (через фабрику)

Для создания UART-фикстур используется фабрика
`_make_uart_fixture` и словарь `_UART_FIXTURE_MAP`. Иначе, пришлось бы
вручную писать фикстуры`uart` / `uart_opto` / ... для каждого таргета.

```python
_UART_FIXTURE_MAP = {
    "uart":             "loaded_host_uart",
    "uart_opto":        "loaded_hil_opto",
    "uart_can":         "loaded_hil_can",
    "uart_button":      "loaded_hil_button",
    "uart_hil_usb_cdc": "loaded_hil_usb_cdc",
}

def _make_uart_fixture(loaded_name: str):
    @pytest.fixture(scope="module")
    def _fixture(request: pytest.FixtureRequest) -> Generator[serial.Serial, None, None]:
        request.getfixturevalue(loaded_name)
        port = request.config.getoption("--vcom")
        with _uart_context(port, cfg.VCOM_BAUD, cfg.READY_TIMEOUT) as ser:
            yield ser
    return _fixture

for _name, _dep in _UART_FIXTURE_MAP.items():
    globals()[_name] = _make_uart_fixture(_dep)
```

Для нового теста достаточно одной строки в `_UART_FIXTURE_MAP`.
Контекстный менеджер `_uart_context` гарантирует `ser.close()` при любом исходе
(исключение, `pytest.fail`, `KeyboardInterrupt`).

### 9.3. Управление стендом (M5 — с teardown)

```python
@pytest.fixture(scope="module")
def m5(request):
    agent = M5Agent(port=cfg.M5_PORT, baud=cfg.M5_BAUD)
    agent.ping()                       # проверить связь
    agent.power(True)                  # RLY1: питание ON
    time.sleep(1.0)                    # ждать POR + стабилизацию
    agent.opto_all_off()               # все реле — baseline
    yield agent
    agent.opto_all_off()               # teardown: сброс сигналов
    agent.power(False)                 # teardown: питание OFF
```

### 9.4. Сброс состояния перед каждым тестом (autouse)

```python
class TestOptoChannels:

    @pytest.fixture(autouse=True)
    def _setup(self, uart_hil_opto, m5):
        self.ser = uart_hil_opto
        self.m5 = m5
        self.m5.opto_all_off()         # baseline перед КАЖДЫМ тестом
        time.sleep(0.15)

    def test_ch1_active(self):
        self.m5.opto_set(1, True)
        time.sleep(0.15)
        assert uart_cmd(self.ser, "OPTO_READ 1") == "ACTIVE"

    def test_ch1_inactive(self):
        # opto_all_off() уже вызван в _setup
        assert uart_cmd(self.ser, "OPTO_READ 1") == "INACTIVE"
```

Без `autouse` пришлось бы указывать `_setup` в каждом тесте вручную.

### 9.5. Активное ожидание вместо `time.sleep`

Фиксированный `sleep` — хрупкий. Лучше опрашивать до готовности:

```python
def wait_until(ser, cmd, expected, timeout_s=1.0):
    """Опрашивать MCU пока ответ не совпадёт с expected."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if uart_cmd(ser, cmd) == expected:
            return
        time.sleep(0.02)
    raise TimeoutError(f"Ожидали {expected!r} от '{cmd}' за {timeout_s} с")
```

Использование:

```python
def test_opto_ch1(self):
    self.m5.opto_set(1, True)
    wait_until(self.ser, "OPTO_READ 1", "ACTIVE", timeout_s=0.5)
```

### 9.6. Ожидание `READY` от прошивки

Паттерн из проекта — прошивка шлёт `READY\r\n` в цикле, пока хост не откроет порт.
Реализован как контекстный менеджер `_uart_context`, который гарантирует закрытие
порта при любом исходе:

```python
@contextmanager
def _uart_context(port: str, baud: int, ready_timeout: float):
    ser = serial.Serial(port=port, baudrate=baud, timeout=2.0, write_timeout=1.0)
    try:
        deadline = time.monotonic() + ready_timeout
        ready = False
        while time.monotonic() < deadline:
            line = ser.readline().decode("ascii", errors="replace").strip()
            if line == "READY":
                ready = True
                break
        if not ready:
            pytest.fail(f"Прошивка не отправила READY за {ready_timeout} с")
        ser.reset_input_buffer()
        yield ser
    finally:
        ser.close()  # выполняется всегда
```

Все `uart_*` фикстуры используют `_uart_context` через фабрику `_make_uart_fixture`
(см. секцию 9.2).

### 9.7. Параметризация фикстур

Один тест — несколько входных наборов. Полезно для проверки всех каналов:

```python
@pytest.fixture(params=[1, 2, 3], ids=["CH1_IN1", "CH2_IN2", "CH3_RS"])
def opto_channel(request):
    return request.param

def test_opto_toggle(self, opto_channel):
    self.m5.opto_set(opto_channel, True)
    time.sleep(0.15)
    resp = uart_cmd(self.ser, f"OPTO_READ {opto_channel}")
    assert resp == "ACTIVE"
    self.m5.opto_set(opto_channel, False)
    time.sleep(0.15)
    resp = uart_cmd(self.ser, f"OPTO_READ {opto_channel}")
    assert resp == "INACTIVE"
```

pytest развернёт это в три теста: `test_opto_toggle[CH1_IN1]`, `[CH2_IN2]`, `[CH3_RS]`.

---

## 10. Типичные ошибки

### Порт не закрыт — следующий файл падает

```python
# ❌ return вместо yield — порт не закроется
@pytest.fixture(scope="module")
def uart(request, loaded_host_uart):
    ser = _open_uart_and_wait_ready(request)
    return ser    # ser.close() никогда не вызовется!

# ✅ Актуальное решение: _uart_context (контекстный менеджер)
# ser.close() гарантирован блоком finally внутри _uart_context.
# Все uart_* фикстуры создаются через _make_uart_fixture,
# который использует _uart_context — ручной close() не нужен.
```

### Нет зависимости от `loaded_*` — UART открывается раньше ELF

```python
# ❌ uart создаётся без гарантии порядка
@pytest.fixture(scope="module")
def uart(request):              # нет loaded_* в сигнатуре!
    ser = _open_uart_and_wait_ready(request)
    yield ser
    ser.close()

# ✅ явная зависимость
@pytest.fixture(scope="module")
def uart(request, loaded_host_uart):   # ← гарантирует: ELF загружен
    ser = _open_uart_and_wait_ready(request)
    yield ser
    ser.close()
```

### Нет зависимости от `m5` — pyOCD без питания

```python
# ❌ MCU обесточен, pyOCD не найдёт пробник
@pytest.fixture(scope="module")
def loaded_hil_opto(request):
    _load_elf(request, Path("...test_hil_opto.elf"))

# ✅ питание включается раньше загрузки
@pytest.fixture(scope="module")
def loaded_hil_opto(request, m5):   # ← m5 включит питание первым
    _load_elf(request, Path("...test_hil_opto.elf"))
```

### `autouse` в `conftest.py` ломает другие файлы

```python
# ❌ flush_buffer вызовется для ВСЕХ тестов, даже тех, что не используют uart
@pytest.fixture(autouse=True)
def flush_buffer(uart):
    uart.reset_input_buffer()

# ✅ autouse только внутри класса
class TestUart:
    @pytest.fixture(autouse=True)
    def _flush(self, uart):
        self.ser = uart
        uart.reset_input_buffer()
```

### Смешение scope — ScopeMismatch

```python
# ❌ module-фикстура зависит от function-фикстуры
@pytest.fixture(scope="function")
def fresh_state():
    return {}

@pytest.fixture(scope="module")
def uart(fresh_state):    # ScopeMismatch!
    ...
```

---

## 11. Шпаргалка: добавить фикстуру для нового HIL-теста

```python
# 1. В conftest.py — загрузка ELF
@pytest.fixture(scope="module")
def loaded_<n>(request, m5):         # m5 только если нужен
    _load_elf(request, Path(cfg.BUILD_DIR) / "tests/target/<n>/test_<n>.elf")

# 2. В conftest.py — одна строка в _UART_FIXTURE_MAP
_UART_FIXTURE_MAP = {
    ...
    "uart_<n>":  "loaded_<n>",       # ← добавить
}
# Фабрика _make_uart_fixture создаст фикстуру автоматически
# с _uart_context (гарантирует ser.close())

# 3. В test_<n>.py — autouse setup
class Test<n>:
    @pytest.fixture(autouse=True)
    def _setup(self, uart_<n>, m5):   # m5 только если нужен
        self.ser = uart_<n>
        self.m5 = m5

    def test_ping(self):
        assert uart_cmd(self.ser, "PING") == "PONG"
```

---

## 12. Визуальная схема жизненного цикла

```bash
pytest test_opto.py

────────────── module scope (один раз на файл) ──────────────

  1. m5()  setup
     ├── M5Agent.connect()
     ├── agent.power(True)         ← RLY1 ON
     ├── sleep(1.0)
     └── agent.opto_all_off()

  2. loaded_hil_opto()             (зависит от m5)
     └── _load_elf()
           ├── pyOCD: halt
           ├── FLEXRAM init
           ├── load ELF segments
           └── run_from_vectors

  3. uart_hil_opto()  setup        (зависит от loaded_hil_opto)
     ├── Serial.open()
     └── wait for "READY\r\n"

─────────── function scope (каждый тест) ────────────────────

  4. _setup()  autouse
     ├── self.ser = uart
     ├── self.m5 = m5
     └── m5.opto_all_off() + sleep

  5. test_ping()                   → uart_cmd("PING") == "PONG" ✅
  6. test_opto_ch1()               → m5.opto_set(1, True) → assert ✅
  7. test_opto_ch2()               → ...

────────────── teardown (обратный порядок) ───────────────────

  8. uart_hil_opto teardown        → ser.close()
  9. loaded_hil_opto teardown      → (нет)
 10. m5 teardown
     ├── agent.opto_all_off()
     └── agent.power(False)        ← RLY1 OFF
```
