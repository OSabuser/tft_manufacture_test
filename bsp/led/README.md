# bsp_led

Драйвер двух пользовательских светодиодов на плате.

---

## Аппаратная часть

| `led_id_t`      | Сигнал     | GPIO  | Pin | Координата | Активный уровень |
| --------------- | ---------- | ----- | --- | ---------- | ---------------- |
| `LED_HEARTBEAT` | `UserLed1` | GPIO3 | 3   | M4         | LOW (0 = горит)  |
| `LED_APP`       | `UserLed2` | GPIO3 | 4   | P2         | LOW (0 = горит)  |

Пины сконфигурированы в `generated/pin_mux.h` (MCUXpresso Config Tools).  
`INIT_GPIO_VALUE = 1U` — оба LED выключены сразу после `led_init()`.

---

## API

```c
void bsp_led_init(void);               // вызвать один раз после board_hw_init()

void bsp_led_on(led_id_t id);
void bsp_led_off(led_id_t id);
void bsp_led_toggle(led_id_t id);
void bsp_led_set(led_id_t id, bool on);
bool bsp_led_get(led_id_t id);
```

---

## Использование

```c
#include "bsp/led.h"

// инициализация
bsp_led_init();

// heartbeat
bsp_led_toggle(LED_HEARTBEAT);

// прикладная индикация
bsp_led_on(LED_APP);    // пакет принят / тест запущен
bsp_led_off(LED_APP);   // сброс
```

---

## CMake

```cmake
target_link_libraries(<target> PRIVATE bsp_led)
```

Зависимости: `bsp_board` (PUBLIC, транзитивно), `sdk_gpio` (PRIVATE).  
При `BUILD_TESTS_HOST=ON` компонент не собирается — мокается через `fff` на уровне теста.

---

## Файлы

```bash
led/
├── CMakeLists.txt
├── include/led.h   # публичный API — без NXP хедеров
├── src/led.c           # реализация, fsl_gpio.h только здесь
└── README.md           # этот файл
```
