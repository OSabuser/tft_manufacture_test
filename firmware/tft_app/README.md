# tft_app

Прошивка лифтового индикатора (MIMXRT1052, TFT 4/7/8/10). Собирается как
MCUboot-слот-образ (Direct-XIP), которым управляет [bootloader](../bootloader/).

- **Архитектура** — [ARCH.md](ARCH.md).
- **План разработки (статус фаз)** — [PLAN.md](PLAN.md).

> `OLD_PROJECT*` и `special/` — референсные дампы старых серийных проектов
> (в `.gitignore`), источник бизнес-логики и доменного дизайна. Не собираются.

---

## Статус: Фаза 0 (каркас)

Минимальный живой каркас: FreeRTOS + heartbeat + кормление унаследованного от
загрузчика WDOG + само-подтверждение слота MCUboot. Доменные/презентационные
слои — со следующих фаз.

## Сборка и подпись

```bash
# devcontainer:
just build::build-app-debug      # app.elf/.bin (Slot A, XIP из слота)
just build::sign-app-debug        # + imgtool EC256 → build/Debug/signed/app_slot_a.bin
just build::test-host             # host-тесты (вкл. test_tft_app_smoke) зелёные
```

Подпись — imgtool ECDSA P-256 (как stub загрузчика), **без** `--confirm`: образ
подтверждает свой слот в рантайме (`boot_set_next`). HAB для app не нужен — его
грузит не BootROM, а загрузчик после проверки imgtool-подписи.

## Прошивка (хост)

```bash
# итеративно — SWD подписанного слот-образа в Slot A (0x60040000), power cycle после:
just host::flash-swd-app-slot-debug
```

## Диагностика (Фаза 0)

USB CDC ACM (VCOM), 115200. Логирует boot-последовательность и **периодически
(раз в 2 с) состояние трейлера слота** (magic/copy_done/image_ok) — видно
независимо от момента подключения терминала:

```bash
screen /dev/cu.usbmodemXXXX 115200      # macOS; порт свой на каждое подключение
```

Транспорт — `port_log_cdc` (переиспользуемый адаптер `utils/log` над
`bsp_usb_cdc`, симметричный `port_log_uart`), best-effort (строки без хоста
молча теряются).

## Раскладка

```bash
src/
├── app/        точка входа, FreeRTOS-задачи, wiring, FreeRTOSConfig.h
├── domain/     чистый C, host-тесты: elevator_model, sul (декодеры), controller
├── services/   адаптеры к железу: gfx, audio_engine, assets, settings, fs
├── ui/         layout-движок, темы, fallback-рендер
└── menu/       экран настроек, навигация двумя кнопками
```

Слот-линкер — [cmake/linker/MIMXRT1052xxxxx_app_slot.ld](../../cmake/linker/MIMXRT1052xxxxx_app_slot.ld).
