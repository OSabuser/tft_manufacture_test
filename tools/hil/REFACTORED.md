# Рефакторинг `conftest.py` — HIL-тесты MIMXRT1052

## Обзор

В ходе code review были выявлены и устранены 3 проблемы:
утечка ресурсов при исключениях, дублирование кода фикстур UART,
несогласованное чтение конфигурации.

---

## 1. Утечка UART-порта при исключении

**Проблема.** Функция `_open_uart_and_wait_ready` открывала `serial.Serial`,
но при исключении внутри цикла ожидания `READY` (например, `SerialException`)
порт не закрывался — операционная система удерживала дескриптор до завершения
процесса.

**Решение.** Функция заменена на контекстный менеджер `_uart_context`,
реализованный через `@contextmanager`. Блок `finally` гарантирует вызов
`ser.close()` при любом исходе.

```python
# До
def _open_uart_and_wait_ready(request) -> serial.Serial:
    ser = serial.Serial(port=port, ...)
    # ... если исключение здесь — ser не закрыт
    return ser

# После
```python
def _uart_context(port: str, baud: int, ready_timeout: float):
    ser = serial.Serial(port=port, baudrate=baud, timeout=2.0, write_timeout=1.0)
    try:
        # … ожидание READY
        yield ser
    finally:
        ser.close()  # выполняется всегда
```

Аналогичная правка применена к фикстуре `usb_cdc_port` — добавлен
`try/finally` вокруг `yield ser`.

---

## 2. Дублирование `uart_*` фикстур

**Проблема.** Пять фикстур (`uart`, `uart_opto`, `uart_can`, `uart_button`,
`uart_hil_usb_cdc`) имели идентичное тело и отличались только зависимостью
`loaded_*`. При добавлении нового теста требовалось вручную копировать
очередную фикстуру.

**Решение.** Введена фабричная функция `_make_uart_fixture` и словарь
`_UART_FIXTURE_MAP`. Все фикстуры генерируются в одну строку через `globals()`.
Зависимость `loaded_*` активируется через `request.getfixturevalue()` —
официальный pytest API (доступен с pytest 3.x).

```python
# До — пять одинаковых блоков
def uart_opto(request, loaded_hil_opto):
    ser = _open_uart_and_wait_ready(request)
    yield ser
    ser.close()

def uart_can(request, loaded_hil_can):
    ser = _open_uart_and_wait_ready(request)
    yield ser\
    ser.close()

# и т.д.

# После Фабрика + словарь
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
        request.getfixturevalue(loaded_name)  # триггерит зависимость явно
        port = request.config.getoption("--vcom")
        with _uart_context(port, cfg.VCOM_BAUD, cfg.READY_TIMEOUT) as ser:
            yield ser
    return _fixture

# Регистрируем все фикстуры в пространстве имён модуля одной строкой
for _name, _dep in _UART_FIXTURE_MAP.items():
    globals()[_name] = _make_uart_fixture(_dep)
```

Для добавления поддержки нового теста теперь достаточно одной строки
в `_UART_FIXTURE_MAP`.

---

## 3. Несогласованное чтение конфигурации

**Проблема.** Большинство параметров читались через `env_config` (`cfg.*`),
но фикстура `usb_cdc_port` обращалась к `os.environ.get()` напрямую.
Это создавало три источника истины, рассыпало дефолтные значения по коду
и лишало возможности переопределить параметр через CLI pytest.

```python
# До — нарушает единообразие конфигурации
port    = os.environ.get("HIL_USB_CDC_PORT", "")
baud    = int(os.environ.get("HIL_USB_CDC_BAUD", "115200"))
timeout = float(os.environ.get("HIL_USB_CDC_TIMEOUT", "5.0"))

# После
@pytest.fixture(scope="module")
def usb_cdc_port(
    uart_hil_usb_cdc,
) -> Generator[serial.Serial, None, None]:
    """Открыть USB CDC порт таргета. Ждёт появления порта и DTR ready."""
    import time

    if not cfg.TARGET_VCOM_PORT:
        pytest.skip("HIL_USB_CDC_PORT not set")

    # Ждём появления USB CDC порта (enumeration после загрузки ELF).
    deadline = time.monotonic() + cfg.TARGET_VCOM_TIMEOUT
    ser = None
    while time.monotonic() < deadline:
        try:
            ser = serial.Serial(port=cfg.TARGET_VCOM_PORT, baudrate=cfg.TARGET_VCOM_BAUD, timeout=0.5)
            break
        except serial.SerialException:
            time.sleep(0.3)

    if ser is None:
        pytest.fail(f"USB CDC port {cfg.TARGET_VCOM_PORT} not available after {cfg.TARGET_VCOM_TIMEOUT}s")

    try:
        # Установить DTR чтобы firmware увидела DTE presence.
        ser.dtr = True
        time.sleep(0.3)
        # Сбросить входной буфер — могут быть мусорные байты от enumeration.
        ser.reset_input_buffer()
        yield ser
    finally:
        ser.close()  

```
