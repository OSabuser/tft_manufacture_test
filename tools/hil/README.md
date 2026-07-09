# tools/hil

Python-окружение на базе [uv](https://docs.astral.sh/uv/) для HIL-тестов и
отладки MIMXRT1052 через pyOCD (SWD: загрузка ELF в RAM, GDB-сервер).

Запускается на **хост-машине** — не внутри devcontainer.

---

## Структура

```bash
tools/hil/
├── conftest.py                — pytest-фикстуры (m5, loaded_<n>, uart_<n>, usb_cdc_port, firmware_cdc)
├── env_config.py               — конфигурация из os.environ / .env
├── pyocd_utils.py               — FLEXRAM init, ELF loader, run_from_vectors (использует conftest.py и load_and_run.py)
├── load_and_run.py              — CLI-утилита: загрузить ELF в RAM вручную
├── 01_test_uart.py              — bsp_uart_host, без внешнего стенда
├── 02_test_opto.py              — bsp_opto через реле M5StampPLC
├── 03_test_can.py               — bsp_can, M5StampPLC как второй узел CAN-шины
├── 04_test_button.py            — bsp_button, интерактивный (кнопки нажимает оператор)
├── 05_test_usb_cdc.py           — bsp_usb_cdc, требует физический USB-порт таргета
├── 06_test_firmware_can.py      — test_can внутри firmware_test (протокол v2 через CDC)
├── 06_test_firmware_opto.py     — test_opto внутри firmware_test (протокол v2 через CDC)
├── m5/
│   ├── agent.py                  — MicroPython-агент на M5StampPLC (реле, входы, CAN)
│   ├── cli.py                    — интерактивный CLI для ручного тестирования стенда и таргета
│   ├── power.py                  — управление питанием таргета (RLY1) из командной строки
│   └── firmware/                 — прошивки MicroPython для M5StampPLC
│       ├── esp32s3_bl-v1.25.0_twai.bin  — используется (поддерживает CAN)
│       └── esp32s3_bl-v1.27.0.bin       — CAN не поддерживается, не использовать
├── pyproject.toml
└── uv.lock
```

> Числовой префикс в имени файла (`NN_test_*.py`, паттерн задан в
> `pyproject.toml` → `python_files`) фиксирует порядок диагностики стенда —
> от простого автономного теста к более сложным и зависящим от внешнего
> оборудования.

> **MicroPython на M5StampPLC:** использовать
> `m5/firmware/esp32s3_bl-v1.25.0_twai.bin`. В прошивке 1.27 модуль CAN
> недоступен — `agent.py` инициализирует CAN при старте, тесты с CAN не
> пройдут. Загрузить прошивку можно с помощью
> [espflash](https://github.com/esp-rs/espflash).

---

## Тестовые файлы

| Файл                        | Что проверяет                                    | Внешний стенд                     | Маркер        |
| --------------------------- | ------------------------------------------------- | ---------------------------------- | ------------- |
| `01_test_uart.py`           | `bsp_uart_host`                                    | нет (M5 только включает питание)   | —             |
| `02_test_opto.py`           | `bsp_opto`                                         | M5StampPLC (реле → оптопары)       | —             |
| `03_test_can.py`            | `bsp_can`                                          | M5StampPLC (второй узел CAN-шины)  | —             |
| `04_test_button.py`         | `bsp_button`                                       | нет, кнопки нажимает оператор      | `interactive` |
| `05_test_usb_cdc.py`        | `bsp_usb_cdc`                                      | физический USB-порт таргета        | `usb_vcom`    |
| `06_test_firmware_can.py`   | `test_can` внутри `firmware_test` (протокол v2)    | M5StampPLC                         | `usb_vcom`    |
| `06_test_firmware_opto.py`  | `test_opto` внутри `firmware_test` (протокол v2)   | M5StampPLC                         | `usb_vcom`    |

Подробности о том, что именно проверяется и какие гарантии даёт каждый
тест — в `tests/target/*/README.md` рядом с соответствующей HIL-прошивкой
(`06_test_firmware_*.py` — исключение: они гоняют `test_can`/`test_opto`
внутри `firmware_test`, а не прошивки из `tests/target`).

---

## Документация

Полное описание стека HIL-тестирования — в `docs/testing/hil/`:

| Документ                                                         | Содержимое                                     |
| ------------------------------------------------------------------ | ----------------------------------------------- |
| [HIL_HOW_TO.md](../../docs/testing/hil/HIL_HOW_TO.md)             | Как запускать HIL-тесты (пошагово)              |
| [HIL_BENCH.md](../../docs/testing/hil/HIL_BENCH.md)               | Стенд: оборудование, подключение, маппинг реле  |
| [HIL_CREATE_TEST.md](../../docs/testing/hil/HIL_CREATE_TEST.md)   | Как добавить новый HIL-тест                     |
| [HIL_FIXTURES.md](../../docs/testing/hil/HIL_FIXTURES.md)         | Памятка по pytest-фикстурам применительно к HIL |

---

## Быстрый старт

```bash
# Первый раз: установить зависимости
cd tools/hil && uv sync

# Заполнить .env в корне репозитория (см. .env.example — порты MCU-Link и M5)

# Задеплоить агент на M5 (после изменений agent.py — повторить)
just host::m5-deploy

# devcontainer: собрать HIL-прошивки
just build::build-hil

# хост: запустить тесты
just host::hil-run              # все не интерактивные и не usb_vcom тесты
just host::hil-run-interactive  # интерактивные (кнопки) — нужен оператор у стенда
just host::hil-uart
just host::hil-opto
just host::hil-can
just host::hil-button
just host::hil-usb-cdc
just host::hil-firmware-opto    # test_opto через firmware_test (протокол v2)
just host::hil-firmware-can     # test_can через firmware_test (протокол v2)
just host::hil-firmware         # оба сразу
```

### Отладка без pytest

```bash
just host::debug-server         # GDB-сервер pyOCD — оставить запущенным, подключаться из VSCode
just host::debug-list-targets   # доступные builtin-таргеты pyOCD для MIMXRT
uv run python load_and_run.py firmware_test.elf   # загрузить ELF в RAM вручную
```

### Работа со стендом M5StampPLC

```bash
just host::m5-scan       # показать подключённые M5Stack-устройства
just host::m5-cli        # интерактивный CLI для ручного тестирования агента
just host::m5-power on   # управление питанием таргета через RLY1 (on|off)
```
