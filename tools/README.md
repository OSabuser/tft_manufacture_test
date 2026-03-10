# Host Tools — окружение для прошивки

Изолированное Python-окружение на базе [uv](https://docs.astral.sh/uv/).
Устанавливается на хост-машине разработчика — **не внутри devcontainer**.

## Структура

```bash
tools/host/
├── dcd/
│   ├── dcd_mt48lc16m16a2.cfg     ← источник истины для DCD (коммитить)
│   ├── dcd_compiler.py           ← скрипт: .cfg → .bin
│   └── dcd.bin                   ← артефакт (пересобирается из .cfg)
│
├── fdcb/
│   ├── fcb_w25q64_quad.yaml      ← конфиг FCB: W25Q64, Quad I/O, 133 MHz
│   └── fcb.bin                   ← артефакт (пересобирается из .yaml)
│
├── hab/
│   ├── hab_firmware_test.yaml    ← HAB: Debug + DCD (входной контроль)
│   ├── hab_bootloader.yaml       ← HAB: Release, без DCD
│   └── hab_app.yaml              ← HAB: Release + DCD (боевая прошивка)
│
├── image/
│   ├── bootimg_firmware_test.yaml
│   ├── bootimg_bootloader.yaml
│   └── bootimg_app.yaml
│
├── pyproject.toml                ← зависимости (spsdk, версия зафиксирована)
├── uv.lock                       ← lockfile (коммитить)
├── .python-version
└── README.md
```

## Установка uv (один раз на машину)

```bash
# Linux/macOS
curl -LsSf https://astral.sh/uv/install.sh | sh

# Windows
powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
```

## Первый запуск (один раз на репозиторий)

```bash
cd tools/host
uv sync
```

### udev правило для USB SDP — только Linux, один раз на машину

Без этого правила `blhost` требует `sudo`:

```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1fc9", ATTR{idProduct}=="0135", MODE="0666", GROUP="plugdev"' \
    | sudo tee /etc/udev/rules.d/99-nxp-sdp.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -a -G plugdev $USER
# Перелогиниться после usermod!
```

## Пайплайн сборки загрузочного образа

### Шаг 1 — DCD (пересобирать при изменении dcd_mt48lc16m16a2.cfg)

```bash
cd tools/host/dcd
uv run python3 dcd_compiler.py dcd_mt48lc16m16a2.cfg dcd.bin
```

### Шаг 2 — FCB (пересобирать при изменении fcb_w25q64_quad.yaml)

```bash
cd tools/host/fdcb
uv run nxpimage bootable-image fcb export -c fcb_w25q64_quad.yaml -o fcb.bin
```

### Шаг 3 — Загрузочный образ

```bash
cd tools/host/image

# firmware_test (Debug, входной контроль)
uv run nxpimage bootable-image export -c bootimg_firmware_test.yaml

# bootloader (Release)
uv run nxpimage bootable-image export -c bootimg_bootloader.yaml

# app (Release, боевая прошивка)
uv run nxpimage bootable-image export -c bootimg_app.yaml
```

Артефакты попадают в `build/Debug/` и `build/Release/` соответственно.

### Проверка образа

```bash
cd tools/host/image
uv run nxpimage bootable-image parse \
    -b ../../../../build/Debug/firmware_test_bootable.bin \
    -f mimxrt1050
```

В выводе должны присутствовать секции `FCB`, `IVT`, `BDT`, `DCD`, `App`.

## Прошивка через USB (BootROM SDP режим)

Перевести плату в SDP режим: `BOOT_MOD_1 = 1` (подтянуть к 3V3), затем подать питание или нажать Reset.

```bash
cd tools/host

# Проверить что устройство определилось (VID:PID 1FC9:0135)
export VID_PID=0x1fc9:0x0130
uv run nxpdevscan

# Версия BootROM на таргете
uv run blhost -u $VID_PID get-property 1 
# Доступные BootROM области памяти
uv run blhost -u $VID_PID list-memory

# Прошить firmware_test
uv run blhost -u $VID_PID flash-erase-all
uv run blhost -u $VID_PID write-memory 0x60000000 \
    ../../build/Debug/firmware_test_bootable.bin
uv run blhost -u $VID_PID reset

# Загрузить в RAM без записи во Flash (для быстрой отладки)
uv run blhost -u $VID_PID load-image ../../build/Debug/firmware_test_bootable.bin
```

После прошивки вернуть `BOOT_MOD_1 = 0` (подтянуть к GND) для нормального старта из Flash.

## Удалённая прошивка (сервер)

```bash
# Из devcontainer или локально — копируем образ на сервер и прошиваем
./tools/host/flash_remote.sh firmware_test Debug
```

## Диагностика

```bash
# Список поддерживаемых семейств (проверка установки)
uv run nxpimage bootable-image get-families

# Список доступных шаблонов для mimxrt1050
uv run nxpimage bootable-image list-templates -f mimxrt1050

# Версия SPSDK
uv run nxpimage --version
uv run blhost --version
```

## Обновление зависимостей

```bash
cd tools/host
uv add "spsdk==X.Y.Z"
uv sync
git add uv.lock
```
