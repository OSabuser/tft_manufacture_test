# tools/host

Python-окружение на базе [uv](https://docs.astral.sh/uv/) для сборки HAB-образов
и прошивки платы через USB SDP или SWD.

Запускается на **хост-машине** — не внутри devcontainer.

---

## Структура

```иbash
tools/host/
├── flash_usb.py      — прошивка через USB ROM: sdphost → Flashloader → Flash
├── flash_swd.py      — прошивка через SWD: FCB + HAB → pyOCD → Flash
├── hab/              — HAB yaml-конфиги для nxpimage (по одному на проект × тип)
├── dcd/
│   ├── ivt_flashloader.bin   — NXP Flashloader (загружается в RAM через SDP)
│   ├── dcd.bin               — DCD: инициализация SDRAM (SEMC + MT48LC16M16A2P)
│   ├── w25q128_fdcb.bin      — FCB для W25Q128 Quad SPI ← используется
│   ├── w25q64_fdcb.bin       — FCB для W25Q64  Quad SPI
│   └── w25q512_fdcb.bin      — FCB для W25Q512 Quad SPI
├── HAB_GUIDE.md      — подробно про HAB-образы и процесс подписи
├── pyproject.toml
└── uv.lock
```

> Все бинарники в `dcd/` получены из NXP SecureProvisioningTool и хранятся
> в репозитории — пересоздавать не нужно.

---

## Документация

Подробное описание обоих способов прошивки — в `docs/HOW_TO_FLASH.md`.
Сравнительная таблица, карта Flash, диагностика — там же.

---

## Быстрый старт

```bash
# Первый раз: установить зависимости
cd tools/host && uv sync

# HAB-образы собираются внутри devcontainer:
just build::hab-firmware-test-debug
just build::hab-all-release

# Прошивка через USB SDP (BOOT_MOD_1 → 3V3 → Reset):
just host::flash-test-debug
just host::flash-test-release
just host::flash-production      # bootloader + app release

# Прошивка через SWD (power cycle обязателен после):
just host::flash-swd-test-debug
just host::flash-swd-test-release
```
