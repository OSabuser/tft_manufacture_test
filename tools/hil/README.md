# tools/hil

Python-окружение на базе [uv](https://docs.astral.sh/uv/) для HIL-тестов,
GDB-сервера отладки и SWD-прошивки через `flash_swd.py`.

Запускается на **хост-машине** — не внутри devcontainer.

---

## Структура

```bash
tools/hil/
├── conftest.py       — pytest-фикстуры (m5, loaded_<n>, uart_<n>)
├── env_config.py     — конфигурация из os.environ / .env
├── pyocd_utils.py    — FLEXRAM init, ELF loader, run_from_vectors
├── load_and_run.py   — CLI-утилита: загрузить ELF в RAM вручную
├── test_uart.py      — HIL тест bsp_uart_host (без стенда)
├── test_opto.py      — HIL тест bsp_opto (через M5StampPLC)
├── m5/
│   ├── agent.py      — MicroPython агент на M5StampPLC (реле, входы, CAN)
│   ├── cli.py        — интерактивный CLI для ручного тестирования стенда и таргета
│   ├── power.py      — управление питанием таргета (RLY1) из командной строки
│   └── firmware/     — прошивки MicroPython для M5StampPLC
│       ├── v1.25/    — MicroPython 1.25 — используется (поддерживает CAN)
│       └── v1.27/    — MicroPython 1.27 — CAN не поддерживается, не использовать
├── pyproject.toml
└── uv.lock
```

> **MicroPython на M5StampPLC:** использовать прошивку из `m5/firmware/v1.25/`.
> В v1.27 модуль CAN недоступен — `agent.py` инициализирует CAN при старте,
> тесты с CAN не пройдут. Загрузить прошивку можно с помощью [утилиты](https://github.com/esp-rs/espflash)

---

## Документация

Полное описание стека HIL-тестирования — в `docs/testing/hil/`:

| Документ | Содержимое |
|----------|-----------|
| [HIL_HOWTO.md](../../docs/testing/hil/HIL_HOWTO.md) | Как запускать HIL-тесты (пошагово) |
| [HIL_BENCH.md](../../docs/testing/hil/HIL_BENCH.md) | Стенд: оборудование, подключение, маппинг реле |
| [HIL_CREATE_TEST.md](../../docs/testing/hil/HIL_CREATE_TEST.md) | Как добавить новый HIL-тест |

---

## Быстрый старт

```bash
# Первый раз: установить зависимости
cd tools/hil && uv sync

# Заполнить .env в корне репозитория (порты MCU-Link и M5)
# Задеплоить агент на M5 (после изменений agent.py — повторить)
just host::m5-deploy

# devcontainer: собрать HIL ELF
just build::build-hil

# хост: запустить тесты
just host::hil-run
just host::hil-uart    # только UART тесты
just host::hil-opto    # только opto тесты
```
