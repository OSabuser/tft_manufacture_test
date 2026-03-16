# HIL Testing Toolchain — MIMXRT1052

> Документ описывает архитектуру и инструментальный стек для Hardware-in-the-Loop (HIL) тестирования на базе NXP MIMXRT1052. Тесты запускаются на рабочих станциях разработчиков, оркестрация — из DevContainer.

---

## Оборудование

| Компонент | Роль |
| :--- | :--- |
| NXP MCU-Link | Отладчик + VCOM-мост (один USB-кабель) |
| MIMXRT1052 (таргет) | Целевое устройство |

MCU-Link предоставляет два логических канала по одному USB:

- **CMSIS-DAP** — прошивка и сброс таргета (control plane)
- **VCOM (USB-UART)** — тестовый вывод с таргета (data plane)

---

## Инструментальный стек

| Задача | Инструмент |
| :--- | :--- |
| Прошивка и сброс таргета | `pyocd` Python API |
| Проброс зонда в DevContainer | `pyocd server --allow-remote` по TCP |
| Чтение тестового вывода | `pyserial` (VCOM) или `socat` TCP-мост |
| Оркестрация HIL-тестов | `pytest` + фикстуры в `conftest.py` |
| Сборка C-кода тестовой прошивки | `CMake` (вызывается из pytest fixture) |
| Тестовый фреймворк в прошивке | Unity + fff |
| Unit-тесты (без железа) | CMake + `CTest` (в DevContainer, без таргета) |
| Интерактивная отладка | `pyocd gdbserver` + VS Code **cortex-debug** |
