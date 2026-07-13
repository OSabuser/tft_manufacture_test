# bsp_boot_state — счётчик попыток загрузки (SRC_GPR)

Счётчик попыток загрузки поверх `SRC` General Purpose Register — переживает тёплый/watchdog-сброс,
обнуляется только на POR. Даёт коду восстановления прожить несколько сбросов подряд без
персистентного хранилища во flash.

---

## Аппаратура

| Параметр                | Значение                                                      |
| ------------------------ | -------------------------------------------------------------- |
| Периферия                | SRC (System Reset Controller)                                  |
| Регистр счётчика          | `SRC_GPR[2]` (GPR3, 0-based индекс `fsl_src` API)               |
| Переживает                | тёплый сброс, watchdog-сброс                                    |
| Обнуляется                | только POR (детект — `SRC->SRSR`, бит `IPP_RESET_B`)             |
| Занято ROM (не трогать)   | GPR1/2 (warm-boot entry/arg), GPR6/7/8/9 (ROM, explicit note в RM), GPR10 (альт. SBMR1) |
| Не занято ROM, но занято конвенцией | GPR5 — RM рекомендует под различение SYSRESETREQ/CPU lockup, не наша задача |

---

## Контракт: разделение с `bsp_wdog`

`SRC->SRSR` и `WDOG1->WRSR` — разные регистры с разной семантикой очистки. `WRSR` самоочищается на
каждый сброс (не требует явной очистки — см. `bsp_wdog_caused_last_reset()`). `SRSR` —
write-1-to-clear и **копит биты между тёплыми сбросами**, если их не чистить софтом:
`bsp_boot_state_init()` чистит `SRSR` при каждом вызове, поэтому вопрос «был ли сброс по watchdog»
остаётся за `bsp_wdog`, а не за этим модулем — этот модуль отвечает только за «был ли сброс POR».

---

## API

```c
void     bsp_boot_state_init(void);       /* взвести — читает/чистит SRC->SRSR, детектит POR */
bool     bsp_boot_state_was_por(void);    /* true, если последний сброс — POR */
uint32_t bsp_boot_attempt_count(void);    /* текущее значение счётчика, 0 сразу после POR */
void     bsp_boot_attempt_inc(void);      /* +1 — звать перед попыткой прыжка в образ */
void     bsp_boot_attempt_reset(void);    /* обнулить — новый образ/фолбэк-стирание */
void     bsp_boot_health_mark(void);      /* = bsp_boot_attempt_reset(), для вызова из приложения */
```

`bsp_boot_state_init()` **не идемпотентна** — повторный вызов в той же сессии увидит уже очищенный
`SRSR` как «не POR». Звать ровно один раз, как можно раньше в `main()`.

---

## Быстрый старт

```c
#include "bsp/boot_state.h"

/* main.c — после board_hw_init()/bsp_wdog_init(): */
bsp_boot_state_init();

if (bsp_boot_attempt_count() >= THRESHOLD)
{
    /* серия сбросов подряд без здорового образа — решение о фолбэке/recovery
     * принимает вызывающий код, не этот модуль */
}

bsp_boot_attempt_inc();   /* перед каждой попыткой прыжка */
/* ... */
bsp_boot_health_mark();   /* приложение подтвердило собственное здоровье */
```

---

## CMake

```cmake
target_link_libraries(firmware_bootloader PRIVATE bsp_boot_state)
```

**Зависимости модуля:**

| Зависимость | Тип     | Описание                                                    |
| ----------- | ------- | -------------------------------------------------------------- |
| `sdk_src`   | PRIVATE | `fsl_src.h` — `SRC_Get/SetGeneralPurposeRegister`, `SRC_Get/ClearResetStatusFlags` |
