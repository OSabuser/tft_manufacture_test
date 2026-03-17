# HOW TO FLASH

Прошивка выполняется **на хосте** (вне devcontainer) через USB ROM (Serial Download Protocol).

---

## 1. Перевод платы в SDP-режим

```bash
1. BOOT_MOD_1 → 3V3
2. Reset
3. Подключить USB к хосту
   → плата определяется как VID:PID 1FC9:0130
```

После прошивки — вернуть в нормальный режим:

```bash
BOOT_MOD_1 → GND → Reset
```

---

## 2. Подготовить HAB-образ (внутри devcontainer)

HAB-образ собирается из ELF-файла командой `nxpimage`. Выполнять в терминале VSCode:

```bash
just build::hab-firmware-test-debug    # → build/Debug/firmware_test_hab.bin
just build::hab-firmware-test-release  # → build/Release/firmware_test_hab.bin
just build::hab-bootloader-release     # → build/Release/bootloader_hab.bin
just build::hab-app-release            # → build/Release/app_hab.bin
just build::hab-all-release            # все три Release за один раз
```

---

## 3. Прошивка (хостовый терминал)

### Запись во Flash

```bash
# Основной рецепт: just host::flash <project> <type>
just host::flash firmware_test debug    # разработка, итерации с отладчиком
just host::flash firmware_test release  # проверить как будет на сервере
just host::flash bootloader release
just host::flash app release
```

### Загрузка в RAM (без записи во Flash)

Быстро, не изнашивает Flash. Плата стартует сразу после загрузки.

```bash
just host::flash-ram firmware_test        # default: debug
just host::flash-ram firmware_test debug
just host::flash-ram firmware_test release
```

### Быстрые алиасы

```bash
just flash                       # = just host::flash firmware_test debug
just host::flash-test-debug      # то же
just host::flash-test-release
just host::flash-production      # bootloader release + app release (с подтверждением)
```

---

## 4. Диагностика

```bash
just host::scan              # найти подключённые NXP USB-устройства
just host::sdp-status        # проверить связь с BootROM (плата в SDP-режиме)
just host::flashloader-status # проверить Flashloader (после jump-address)
just host::check-deps        # проверить версии just / uv / docker
```

---

## 5. Что происходит при прошивке

```bash
Плата в SDP-режиме (1FC9:0130)
  │
  ├── sdphost: загрузить ivt_flashloader.bin в RAM (0x20001C00)
  └── sdphost: jump-address → Flashloader поднимается как 15A2:0073
        │
        ├── configure-memory (0xC0000007) — инициализация FlexSPI NOR
        ├── flash-erase-region 0x60000000
        ├── configure-memory (0xF000000F) — запись FCB в 0x60000000
        ├── write-memory 0x60001000 ← HAB-образ
        └── reset
```

---

## 6. Производственный сценарий (сервер)

```bash
just host::incoming     # firmware_test release → HIL-тесты периферии
just host::production   # bootloader release + app release
```
