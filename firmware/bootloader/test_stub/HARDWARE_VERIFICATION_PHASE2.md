# Фаза 2 (bootutil / MCUboot Direct-XIP) — аппаратная верификация

Памятка с готовыми командами: сборка, прошивка, стирание, чек-лист. 

Все команды — из корня репозитория (`tft_manufacture_test/`).

---

## 0. Предпосылки

- `firmware/bootloader` собран с ARM-стороной Фазы 2 (bootutil + `flash_map_backend.c` над
  `bsp_qspi_flash` + `boot_select.c`). `main.c` пытается `boot_go()` сразу после минимального
  bring-up, до подъёма USB CDC — так что плата грузит tft_app и без подключённого кабеля.
- Заглушка вместо ещё не существующего `tft_app` — `firmware/bootloader/test_stub/`: два образа,
  различающиеся частотой мигания `LED_APP` (по частоте видно, какой слот реально выбрал bootloader).
- Оба образа подписаны тестовым sample-ключом MCUboot (`sdk/middleware/mcuboot_opensource/root-ec-p256.pem`)
  — публичный, не для продакшена, годится только для этой проверки.

---

## 1. Сборка

```bash
# Bootloader (Debug) + HAB-контейнер
just build::build-bootloader-debug
just build::hab-bootloader-debug

# Заглушки Slot A/Б — собрать И подписать imgtool'ом одной командой
just build::build-mcuboot-stub
```

После этого в `build/Debug/` должны появиться:

```bash
bootloader_hab.bin
signed/stub_a_v1_confirmed.bin      (2 МБ, v1.0.0, --confirm)
signed/stub_b_v2_confirmed.bin      (2 МБ, v2.0.0, --confirm)
signed/stub_a_v1_unconfirmed.bin    (2 МБ, v1.0.0, без --confirm — для revert)
```

Если файлов нет или размер не 2 МБ — пересобрать:
`rm -rf build/Debug && just build::build-bootloader-debug && just build::hab-bootloader-debug && just build::build-mcuboot-stub`.

---

## 2. Прошивка bootloader

```bash
just host::flash-swd-bootloader-debug
# обязателен power cycle платы после прошивки — SWD-запись не ресетит автоматически
```

---

## 3. Прошивка образов в слоты (pyOCD)

Не через `flash_swd.py` — тот собирает FCB+IVT+HAB под `0x60000000`, слотам это не нужно (не
самостоятельный boot-образ для BootROM, а данные, которые читает `boot_go()`). Пишем сырой
подписанный `.bin` напрямую по адресу слота.

**Важно:** `uv run --directory tools/hil` меняет рабочую директорию у самого `pyocd`, не только у
`uv` — путь к `.bin` должен быть абсолютным (`"$(pwd)/..."`), иначе резолвится от `tools/hil/` и
получите `No such file`.

```bash
# Slot A (0x60040000) — валидный, confirmed
uv run --directory tools/hil pyocd flash --target mimxrt1050_quadspi --frequency 4000000 \
  --base-address 0x60040000 --erase sector "$(pwd)/build/Debug/signed/stub_a_v1_confirmed.bin"

# Slot Б (0x60240000) — валидный, confirmed, версия новее
uv run --directory tools/hil pyocd flash --target mimxrt1050_quadspi --frequency 4000000 \
  --base-address 0x60240000 --erase sector "$(pwd)/build/Debug/signed/stub_b_v2_confirmed.bin"

# Slot A — неподтверждённый вариант, для проверки revert (использовать ВМЕСТО confirmed-варианта)
uv run --directory tools/hil pyocd flash --target mimxrt1050_quadspi --frequency 4000000 \
  --base-address 0x60040000 --erase sector "$(pwd)/build/Debug/signed/stub_a_v1_unconfirmed.bin"
```

---

## 4. Стирание слота

Адрес — позиционный аргумент (не `-a`), формат диапазона `start+length` (не `start@length`).

```bash
uv run --directory tools/hil pyocd erase --target mimxrt1050_quadspi --frequency 4000000 \
  --sector 0x60040000+0x200000   # Slot A, 2 МБ

uv run --directory tools/hil pyocd erase --target mimxrt1050_quadspi --frequency 4000000 \
  --sector 0x60240000+0x200000   # Slot Б, 2 МБ
```

---

## 5. Проверка "bootloader жив" (CDC)

Актуально для сценария 4 (оба слота пусты — bootloader не прыгает, остаётся в диагностическом цикле).
Подключиться к USB CDC ACM платы (см. [README.md](README.md#3-подключение)) любым терминалом:

```bash
screen /dev/cu.usbmodemXXXX   # macOS, порт свой у каждого подключения
```

Отправить `{"type":"cmd","cmd":"ping"}` — в ответ должно прийти `{"type":"pong"}`.

---

## 6. Чек-лист сценариев

Между КАЖДЫМ сценарием — обязательный power cycle платы (SWD-запись не ресетит автоматически).

| №   | Сценарий                        | Подготовка                                                                                                                                                                                                     | Ожидаемый результат                                                                                                           |
| --- | ------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| 1   | Валиден только Slot A           | Erase Slot Б (шаг 4), `stub_a_v1_confirmed.bin` → Slot A (шаг 3)                                                                                                                                               | `LED_APP` мигает ~1 раз/сек                                                                                                   |
| 2   | Оба валидны, побеждает версия Б | + `stub_b_v2_confirmed.bin` → Slot Б                                                                                                                                                                           | `LED_APP` мигает ~2 раза/сек                                                                                                  |
| 3   | Slot Б повреждён                | В Slot Б — испорченный файл (скопировать `stub_b_v2_confirmed.bin`, поменять один байт в payload, прошить тем же способом что и в шаге 3)                                                                      | `LED_APP` возвращается к ~1 разу/сек (снова Slot A)                                                                           |
| 4   | Оба слота пусты                 | Erase Slot A и Slot Б целиком (шаг 4, оба вызова)                                                                                                                                                              | `LED_APP` не мигает по образцу заглушки; `ping` по CDC (шаг 5) отвечает `pong` — bootloader не прыгнул, остался в своём цикле |
| 5   | Revert неподтверждённого образа | `stub_a_v1_unconfirmed.bin` → Slot A; power cycle (1) → LED мигает (образ выбран впервые, `copy_done` выставляется); power cycle (2) БЕЗ каких-либо действий между ними → Slot A должен быть стёрт bootutil'ом | После второго ресета — как сценарий 4 (LED не мигает, CDC ping жив)                                                           |

---
