# Добавление нового HIL-теста

## Обзор стека

```bash
devcontainer                         хост
─────────────────────────────────    ────────────────────────────────────
tests/target/<name>/                 tools/hil/
  main.c        ← C-прошивка с CLI     test_<name>.py  ← pytest-тесты
  CMakeLists.txt                        conftest.py     ← фикстуры (общие)
                                        m5/agent.py     ← агент M5 (если нужен)
CMakePresets.json
  target-debug-build                 just/host.just
  └── targets: [test_<name>]           hil-run, hil-<name>

just/build.just
  build-hil
```

Три типа тестов:

| Тип | Использует M5 | Запуск | Когда применять |
|-----|--------------|--------|-----------------|
| **Базовый** | Нет | `hil-run` | Тестирование UART CLI, алгоритмов, таймингов |
| **С M5** | Да | `hil-run` | Тестирование GPIO, оптовходов, реле, питания |
| **Интерактивный** | Нет / Да | `hil-run-interactive` | Периферия требует действий оператора: кнопки, дисплей |

Интерактивные тесты помечаются `@pytest.mark.interactive` и **никогда не входят в `hil-run`** — они требуют живого оператора и не пригодны для CI.

---

## Шаг 1 — C-прошивка: `tests/target/<name>/`

### `main.c` — шаблон

```c
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include <string.h>

#define CLI_BAUD_RATE  115200U
#define CLI_LINE_MAX   128U
#define CLI_RX_TIMEOUT 100U   /* мс — увеличить если нужен частый process() */

static size_t cli_read_line(uint8_t *p_buf, size_t max_len)
{
    size_t pos = 0U;
    while (pos < (max_len - 1U)) {
        int32_t byte = bsp_uart_host_read_byte(CLI_RX_TIMEOUT);
        if (byte < 0)           break;
        if ((char)byte == '\r') continue;
        if ((char)byte == '\n') break;
        p_buf[pos++] = (uint8_t)byte;
    }
    p_buf[pos] = '\0';
    return pos;
}

static void cli_process_line(const char *p_line)
{
    if (strncmp(p_line, "PING", 4U) == 0) {
        bsp_uart_host_write_str("PONG\r\n");
    }
    /* TODO: добавить команды */
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

    /* Шлём READY пока хост не открыл порт */
    while (bsp_uart_host_rx_available() == 0U) {
        bsp_uart_host_write_str("READY\r\n");
        bsp_delay(200U);
    }

    static uint8_t s_line_buf[CLI_LINE_MAX];
    for (;;) {
        /* Если тест использует прерывания/process() — вызывать здесь */
        size_t len = cli_read_line(s_line_buf, sizeof(s_line_buf));
        if (len > 0U) cli_process_line((const char *)s_line_buf);
    }
}
```

### `CMakeLists.txt`

```cmake
set(TARGET_NAME test_<name>)

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
    # + bsp_opto / bsp_can / ... если нужно
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
    COMMENT "Size: ${TARGET_NAME}")
```

---

## Шаг 2 — Подключить в `tests/target/CMakeLists.txt`

```cmake
add_subdirectory(host_uart)
add_subdirectory(hil_opto)
add_subdirectory(<name>)   # ← добавить строку
```

---

## Шаг 3 — `CMakePresets.json`: добавить таргет

```json
{
    "name": "target-debug-build",
    "configurePreset": "target-debug",
    "targets": [
        "test_host_uart",
        "test_hil_opto",
        "test_<name>"
    ]
}
```

---

## Шаг 4 — Сборка

```bash
# В devcontainer:
just build::build-hil

# Проверить что новый таргет собрался:
ls build/target-debug/tests/target/<name>/test_<name>.elf
```

---

## Шаг 5 — `conftest.py`: добавить фикстуры

Открыть `tools/hil/conftest.py` и добавить в конец раздела с фикстурами загрузки.

### Базовый тест (без M5)

```python
@pytest.fixture(scope="module")
def loaded_<name>(request: pytest.FixtureRequest) -> None:
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/<name>/test_<name>.elf",
    )

@pytest.fixture(scope="module")
def uart_<name>(
    request: pytest.FixtureRequest,
    loaded_<name>,              # ← гарантирует порядок: ELF раньше UART
) -> Generator[serial.Serial, None, None]:
    ser = _open_uart_and_wait_ready(request)
    yield ser
    ser.close()
```

### Тест с M5 (GPIO, реле, питание)

```python
@pytest.fixture(scope="module")
def loaded_<name>(request: pytest.FixtureRequest, m5: M5Agent) -> None:
    """
    Зависит от m5 — питание таргета уже включено к моменту загрузки ELF.
    """
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/<name>/test_<name>.elf",
    )

@pytest.fixture(scope="module")
def uart_<name>(
    request: pytest.FixtureRequest,
    loaded_<name>,
) -> Generator[serial.Serial, None, None]:
    ser = _open_uart_and_wait_ready(request)
    yield ser
    ser.close()
```

**Правило:** если тест управляет железом через M5 — `loaded_<name>` должен явно
зависеть от `m5`. Это гарантирует что питание включено до того как pyOCD
попытается подключиться к MCU.

---

## Шаг 6 — `tools/hil/test_<name>.py`

### Базовый тест (без M5)

```python
"""test_<name>.py — HIL тест <что тестируем>."""
import pytest
from conftest import uart_cmd


class Test<Name>:

    @pytest.fixture(autouse=True)
    def _setup(self, uart_<name>):
        self.ser = uart_<name>

    def test_ping(self):
        """Базовая проверка канала."""
        assert uart_cmd(self.ser, "PING") == "PONG"

    def test_something(self):
        resp = uart_cmd(self.ser, "MY_CMD")
        assert resp == "EXPECTED"
```

### Тест с M5

```python
"""test_<name>.py — HIL тест <что тестируем> через M5StampPLC."""
import time
import pytest
from conftest import uart_cmd

SETTLE_S = 0.15   # ждать после переключения реле


class Test<Name>:

    @pytest.fixture(autouse=True)
    def _setup(self, uart_<name>, m5):
        self.ser = uart_<name>
        self.m5 = m5
        self.m5.opto_all_off()   # или другой сброс состояния стенда
        time.sleep(SETTLE_S)

    def test_ping(self):
        assert uart_cmd(self.ser, "PING") == "PONG"

    def test_m5_ping(self):
        self.m5.ping()

    def test_something_with_relay(self):
        self.m5.opto_set(1, True)
        time.sleep(SETTLE_S)
        assert uart_cmd(self.ser, "READ_INPUT") == "ACTIVE"
```

**Важно про таймауты:** после переключения реле нужно ждать:
реле (~10 мс) + оптопара (~0.1 мс) + дебаунс прошивки + один цикл `process()`.
Используй активное ожидание вместо фиксированного `sleep` там где важна скорость:

```python
def wait_until(ser, cmd, expected, timeout_s=1.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if uart_cmd(ser, cmd) == expected:
            return
        time.sleep(0.02)
    raise TimeoutError(f"Ожидали {expected!r} от '{cmd}'")
```

### Интерактивный тест (оператор нажимает кнопки / смотрит на дисплей)

Добавить маркер на класс. Для ввода использовать `/dev/tty` напрямую — `input()` не работает под захватом pytest даже с `-s`:

```python
"""test_<name>.py — интерактивный HIL-тест <что тестируем>."""
import time
import pytest
from conftest import uart_cmd

SETTLE_S = 0.10   # ждать после действия оператора (debounce и т.п.)


def _operator_prompt(msg: str) -> None:
    """Вывести подсказку и дождаться Enter от оператора.
    Читает /dev/tty напрямую — работает независимо от захвата pytest."""
    with open("/dev/tty", "w") as tty_out:
        tty_out.write(f"\n  >>> {msg}\n      Нажмите Enter когда готово...\n")
        tty_out.flush()
    with open("/dev/tty", "r") as tty_in:
        tty_in.readline()
    time.sleep(SETTLE_S)


@pytest.mark.interactive
class Test<Name>:

    @pytest.fixture(autouse=True)
    def _setup(self, uart_<name>):
        self.ser = uart_<name>

    def test_ping(self):
        assert uart_cmd(self.ser, "PING") == "PONG"

    def test_something(self):
        _operator_prompt("Выполни действие X на плате")
        assert uart_cmd(self.ser, "MY_CMD") == "EXPECTED"
```

**Запуск интерактивных тестов:**

```bash
just host::hil-run-interactive   # все интерактивные
just host::hil-button            # конкретный интерактивный
```

**Правило:** интерактивные тесты **не добавлять** в `hil-run` — они входят только в `hil-run-interactive`.

---

## Шаг 7 — `just/host.just`: добавить рецепт (опционально)

```just
[doc('Запустить HIL-тест <name>')]
[group('hil')]
hil-<name>:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest test_<name>.py -v
```

Для интерактивных — добавить флаг `-s`:

```just
[doc('Запустить HIL-тест <n> (интерактивный, требует оператора)')]
[group('hil')]
hil-<n>:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest test_<n>.py -s -v
```

---

## Полный цикл

```bash
# 1. devcontainer — собрать прошивку
just build::build-hil

# 2. хост — убедиться что стенд готов (если тест использует M5)
just host::m5-deploy       # если менялся agent.py
just host::m5-scan         # убедиться что M5 видна

# 3. хост — запустить только новый тест
just host::hil-<name>

# 4. хост — загрузить ELF вручную без тестов (для отладки)
uv run --directory tools/hil python load_and_run.py \
    build/target-debug/tests/target/<name>/test_<name>.elf

# 5. хост — запустить один тест
uv run --directory tools/hil pytest test_<name>.py::Test<Name>::test_ping -v
```

---

## Как работают фикстуры

### Цепочка зависимостей

```bash
test_foo()
  └── _setup (function scope, autouse)
        ├── uart_<n> (module scope)     ← открыт один раз на весь файл
        │     └── loaded_<n>            ← ELF загружен один раз
        │           └── m5              ← (если нужен) питание включено
        └── m5 (module scope)           ← (если нужен напрямую в тесте)
```

`scope=module` — фикстура создаётся один раз на весь тест-файл, уничтожается
после последнего теста. ELF грузится один раз, порт открывается один раз.

### Порядок при запуске нескольких файлов

```bash
pytest test_uart.py test_<name>.py

test_uart.py               test_<name>.py
─────────────────────      ─────────────────────
loaded_host_uart           m5 ← создаётся
uart ← создаётся           loaded_<name>
  test_ping                uart_<name> ← создаётся
  test_echo                  test_ping
uart.close()                 test_something
                           uart_<name>.close()
                           m5 teardown → power(False)
```

Каждый файл — своя загрузка ELF, свой UART-сеанс. MCU перезагружается между файлами.

---

## Чеклист

### Автоматический тест (базовый или с M5)

```bash
[ ] tests/target/<n>/main.c           — C-прошивка с CLI + READY-паттерн
[ ] tests/target/<n>/CMakeLists.txt   — сборка с bsp_boot_ram
[ ] tests/target/CMakeLists.txt          — add_subdirectory(<n>)
[ ] CMakePresets.json                    — добавить test_<n> в targets
[ ] tools/hil/conftest.py                — loaded_<n> + uart_<n>
[ ] tools/hil/test_<n>.py             — pytest-тесты
[ ] just/host.just                       — рецепт hil-<n> (опционально)
[ ] just build::build-hil                — зелёная сборка
[ ] just host::hil-<n>                — зелёный прогон
```

### Интерактивный тест (дополнительно к базовому чеклисту)

```bash
[ ] @pytest.mark.interactive             — пометить класс в test_<n>.py
[ ] just/host.just                       — рецепт hil-<n> с флагом -s
[ ] just host::hil-run-interactive       — зелёный прогон
[ ] убедиться что just host::hil-run     — NOT в выборке (маркер исключает)
```
