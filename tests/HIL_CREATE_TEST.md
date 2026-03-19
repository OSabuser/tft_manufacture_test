# Добавление нового HIL-теста

## Обзор стека

```bash
devcontainer                        хост
────────────────────────────────    ──────────────────────────────────────
tests/target/<name>/                tools/hil/
  main.c          ← C-прошивка        test_<name>.py  ← pytest-тесты
  CMakeLists.txt                       conftest.py     ← фикстуры (общие)

CMakePresets.json                   just/host.just
  target-debug-build                  hil-run, hil-smoke ...
  └── targets: [test_<name>]

just/build.just
  build-hil
```

---

## Шаг 1 — C-прошивка: `tests/target/<name>/`

### `main.c`

Минимальный шаблон для нового теста:

```c
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include <string.h>

#define CLI_BAUD_RATE  115200U
#define CLI_LINE_MAX   128U
#define CLI_RX_TIMEOUT 100U

static size_t cli_read_line(uint8_t *p_buf, size_t max_len)
{
    size_t pos = 0U;
    while (pos < (max_len - 1U)) {
        int32_t byte = bsp_uart_host_read_byte(CLI_RX_TIMEOUT);
        if (byte < 0)               break;
        if ((char)byte == '\r')     continue;
        if ((char)byte == '\n')     break;
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
    /* TODO: добавить команды для нового теста */
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
    # + bsp_can / bsp_sdio / ... если нужно
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
    COMMENT "Size: ${TARGET_NAME}"
)
```

---

## Шаг 2 — Подключить в `tests/target/CMakeLists.txt`

```cmake
add_subdirectory(host_uart)
add_subdirectory(<name>)   # ← добавить строку
```

---

## Шаг 3 — `CMakePresets.json`: добавить таргет в `target-debug-build`

```json
{
    "name": "target-debug-build",
    "configurePreset": "target-debug",
    "targets": [
        "test_host_uart",
        "test_<name>"
    ]
}
```

---

## Шаг 4 — `just/build.just`: `build-hil` пересобирает всё автоматически

Ничего менять не нужно — `build-hil` вызывает `cmake --build --preset target-debug-build`,
а пресет уже знает про новый таргет после Шага 3.

```bash
# Проверить что новый таргет собирается:
just build::build-hil
```

---

## Шаг 5 — Python-тест: `tools/hil/test_<name>.py`

```python
"""test_<name>.py — HIL тест <что тестируем>."""
import pytest
from conftest import uart_cmd


class Test<Name>:

    @pytest.fixture(autouse=True)
    def _setup(self, loaded_<name>, uart):
        self.ser = uart

    @pytest.mark.smoke
    def test_ping(self):
        """Базовая проверка канала."""
        assert uart_cmd(self.ser, "PING") == "PONG"

    def test_something(self):
        """Описание теста."""
        resp = uart_cmd(self.ser, "MY_CMD arg")
        assert resp == "EXPECTED"
```

---

## Шаг 6 — `conftest.py`: добавить фикстуру загрузки

```python
@pytest.fixture(scope="module")
def loaded_<name>(request: pytest.FixtureRequest) -> None:
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/<name>/test_<name>.elf",
    )
```

Фикстура `uart` уже зависит от `loaded_host_uart`. Для нового теста нужна
своя пара: `loaded_<name>` + при необходимости своя `uart_<name>` если нужен
отдельный порт или скорость. Обычно достаточно переиспользовать `uart`.

---

## Шаг 7 — `just/host.just`: добавить рецепты (опционально)

Для часто используемых тестов удобно добавить алиасы:

```just
[group('hil')]
hil-run-<name>:
    HIL_BUILD_DIR={{_hil_build}} \
        uv run --directory {{HIL_DIR}} pytest test_<name>.py -v
```

Если алиас не нужен — `just host::hil-run` запускает **все** тесты из `tools/hil/`
автоматически (pytest обходит все `test_*.py`).

---

## Полный цикл

```bash
# 1. devcontainer — собрать прошивку
just build::build-hil

# 2. хост — запустить все HIL тесты (включая новый)
just host::hil-run

# 3. хост — только новый тест
just host::hil-run-<name>

# 4. хост — загрузить ELF без тестов (для ручной отладки)
uv run --directory tools/hil python load_and_run.py \
    build/target-debug/tests/target/<name>/test_<name>.elf
```

---

## Как работают фикстуры

### Общая схема зависимостей

Для каждого тест-модуля цепочка фикстур одна и та же:

```bash
test_foo()
  └── _setup (scope=function, autouse)
        ├── loaded_<n> (scope=module)   ← грузит ELF на MCU
        └── uart       (scope=module)   ← открывает порт, ждёт READY
              └── depends_on: loaded_<n>  ← гарантирует порядок
```

`scope=module` означает: фикстура создаётся один раз на весь файл с тестами
и уничтожается после последнего теста в нём. Все тесты внутри одного файла
разделяют один и тот же экземпляр — ELF загружается один раз, порт открывается
один раз.

### Порядок вызовов внутри одного модуля

```bash
──────────────────────────────────── module scope (один раз на файл)

1. loaded_<n>()
   └── _load_elf()
         ├── open_target()     → pyOCD: подключиться к MCU
         ├── flexram_init()    → настроить ITCM/DTCM
         ├── load_elf()        → записать PT_LOAD сегменты по адресам
         └── run_from_vectors()→ SP/PC из 0x00000000/0x00000004 → resume

2. uart()                      (зависит от loaded_<n>, создаётся после)
   ├── serial.Serial.open()
   ├── while readline() != "READY":  ← ждём сигнал от прошивки
   └── yield ser              → порт готов к работе

──────────────────────────────────── function scope (каждый тест)

3. _setup()
   └── self.ser = uart        → просто сохранить ссылку

4. test_ping()                → uart_cmd(self.ser, "PING") == "PONG"
5. test_echo_simple()
6. ... остальные тесты

──────────────────────────────────── teardown (в обратном порядке)

7. uart teardown               → ser.close()
8. loaded_<n> teardown         → (нет, возвращает None)
```

### Почему `uart` явно зависит от `loaded_<n>`

```python
def uart(request, loaded_<n>):   # ← зависимость объявлена в сигнатуре
    ...
```

Без этой зависимости pytest мог бы создать `uart` раньше чем ELF загружен.
Порт бы открылся, но `READY` не пришёл бы — таймаут и падение. Явная
зависимость в сигнатуре — единственный надёжный способ гарантировать порядок.

### Что происходит при запуске нескольких тест-файлов

```bash
pytest test_uart.py test_can.py
```

```bash
test_uart.py                      test_can.py
──────────────────────────────    ──────────────────────────────
loaded_host_uart  ← создаётся    loaded_can  ← создаётся
uart              ← создаётся    uart        ← создаётся заново
  test_ping                         test_can_send
  test_echo                         test_can_receive
uart.close()      ← teardown     uart.close()  ← teardown
```

Каждый файл получает **свою** загрузку ELF и свой сеанс UART. MCU
перезагружается между файлами — это правильно, у каждого теста своя прошивка.

### Что происходит при запуске одного теста из модуля

```bash
uv run pytest test_uart.py::TestUartBasic::test_echo_simple -v
```

Несмотря на то что запущен один тест, `scope=module`-фикстуры всё равно
создаются: ELF загружается, порт открывается, READY ожидается. Это цена за
изоляцию — зато тест полностью самодостаточен.

### Шаблон `conftest.py` для нового теста

Для каждого нового тест-модуля нужно добавить только одну фикстуру — `loaded_<n>`.
Всё остальное (`uart`, `uart_cmd`, `open_target`, `flexram_init`) переиспользуется:

```python
# conftest.py — добавить:

@pytest.fixture(scope="module")
def loaded_<n>(request: pytest.FixtureRequest) -> None:
    _load_elf(
        request,
        Path(cfg.BUILD_DIR) / "tests/target/<n>/test_<n>.elf",
    )

# Если нужна отдельная uart-фикстура (другой порт, другой бод):
@pytest.fixture(scope="module")
def uart_<n>(
    request: pytest.FixtureRequest,
    loaded_<n>,             # ← порядок гарантирован
) -> Generator[serial.Serial, None, None]:
    port = request.config.getoption("--vcom")
    ser = serial.Serial(port=port, baudrate=115200, timeout=2.0)
    # ждём READY ...
    yield ser
    ser.close()
```

В большинстве случаев отдельная `uart_<n>` не нужна — стандартная `uart`
работает для любого теста, потому что протокол (`READY` + текстовые команды)
одинаковый для всех прошивок.

---

## Чеклист

```bash
[ ] tests/target/<name>/main.c          — C-прошивка с CLI
[ ] tests/target/<name>/CMakeLists.txt  — сборка с bsp_boot_ram
[ ] tests/target/CMakeLists.txt         — add_subdirectory(<name>)
[ ] CMakePresets.json                   — добавить test_<name> в targets
[ ] tools/hil/test_<name>.py            — pytest-тесты
[ ] tools/hil/conftest.py               — добавить loaded_<name> фикстуру
[ ] just/host.just                      — алиас hil-run-<name> (опционально)
```
