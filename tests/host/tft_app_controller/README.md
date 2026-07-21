# test_tft_app_controller

## Модуль под тестом

`firmware/tft_app/src/domain/controller/src/controller.c` — редьюсер
`sul_result_t` (+кэш) → `indication_task_t` (diff). Плюс
`controller/src/mode_priority.c` (свёртка сигналов в экранный режим) и
`elevator_model.c`.

## Категория

A — платформонезависимый, без HAL-вызовов и моков.

## Что проверяется

**Diff контроллера:**
- Кэш засеян дефолтом при init: результат, равный дефолту, — не изменение.
- Раздельные pending-флаги (`pos`, `next`, `direction`, `mode`) — помечается
  только реально изменившееся поле.
- Таймаут связи — тем же путём, что обычный кадр (`controller_process()` с
  `sul_default_state()`), без отдельного API.
- `arrival_pending` / `movement_pending` — по **фронту** `false→true` (уровень
  держится → повторно не взводится).

**Таблица приоритетов (`sul_resolve_mode`):**
- Нет сигналов → `SUL_MODE_NORMAL`.
- Каждый одиночный сигнал → свой режим.
- Порядок при одновременной активности: fireman > пожар > перегруз > сейсмо >
  сервис > погрузка (проверка каждой соседней пары).
- `mode_pending` поднимается на смену **разрешённого** режима, а не сырого
  сигнала (перегруз при активном пожаре режим не меняет → `mode_pending` = false).

## Гарантии

- Приоритет определяется порядком строк таблицы-данных, не значениями enum.
- `indication_task_t.mode` несёт разрешённый режим нового результата (презентация
  читает его при `mode_pending`).

## Запуск

```bash
ctest --preset host-debug-test -R test_tft_app_controller -V
```
