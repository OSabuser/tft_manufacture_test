# Windows Host Results

## Step 1: spike_hab.py

```bash
PS C:\Development\IMXRT\tft_manufacture_test\tools\production> uv run pytest .\spike\spike_hab.py -v -s
============================================================ test session starts =============================================================
platform win32 -- Python 3.13.14, pytest-9.1.1, pluggy-1.6.0 -- C:\Development\IMXRT\tft_manufacture_test\tools\production\.venv\Scripts\python.exe
cachedir: .pytest_cache
rootdir: C:\Development\IMXRT\tft_manufacture_test\tools\production
configfile: pyproject.toml
collected 2 items                                                                                                                             

spike/spike_hab.py::test_golden_hab_no_dcd PASSED
spike/spike_hab.py::test_golden_hab_with_dcd PASSED

```

## Step 2: spike_flash.py --ports-only

```bash
# подключены M5StamPLC + плата с firmware_test

PS C:\Development\IMXRT\tft_manufacture_test\tools\production> uv run python .\spike\spike_flash.py --ports-only
=== Serial-порты (serial.tools.list_ports.comports) ===
  COM10                 VID:PID=1996:00AD  'Устройство с последовательным интерфейсом USB (COM10)'  serial=''
  COM8                  VID:PID=303A:4001  'Устройство с последовательным интерфейсом USB (COM8)'  serial='3CDC75EDCF6C0000'

  Сверь: firmware_test ожидается как 1996:00AD. Для M5StampPLC запиши VID:PID из вывода выше — это и есть измерение О1 (см. MONOLITH_APP_PLAN.md §О1 и DEV_ARCH.md §5).
```

## Step 3: spike_flash.py 

```bash
# подключены M5StamPLC + плата в режиме SDP
PS C:\Development\IMXRT\tft_manufacture_test\tools\production> uv run python .\spike\spike_flash.py             
=== Serial-порты (serial.tools.list_ports.comports) ===
  COM8                  VID:PID=303A:4001  'Устройство с последовательным интерфейсом USB (COM8)'  serial='3CDC75EDCF6C0000'

  Сверь: firmware_test ожидается как 1996:00AD. Для M5StampPLC запиши VID:PID из вывода выше — это и есть измерение О1 (см. MONOLITH_APP_PLAN.md §О1 и DEV_ARCH.md §5).

=== SDP/Flashloader (SDP=0x1fc9:0x0130, Flashloader=0x15a2:0x0073) ===
  Загрузка Flashloader через SDP (0x1fc9:0x0130)
  Ожидание Flashloader (до 10с).... OK

✅ get_property(CURRENT_VERSION) = [1258424320]

=== configure_memory (FlexSPI NOR) ===
configure_memory: OK

✅ Гейт 0 (SDP/Flashloader): пройден
```

## Step 4: spike_flash.py --erase-all

```bash
# Подключены M5StamPLC + плата в режиме SDP

PS C:\Development\IMXRT\tft_manufacture_test\tools\production> uv run python .\spike\spike_flash.py --erase-all
=== Serial-порты (serial.tools.list_ports.comports) ===
  COM8                  VID:PID=303A:4001  'Устройство с последовательным интерфейсом USB (COM8)'  serial='3CDC75EDCF6C0000'

  Сверь: firmware_test ожидается как 1996:00AD. Для M5StampPLC запиши VID:PID из вывода выше — это и есть измерение О1 (см. MONOLITH_APP_PLAN.md §О1 и DEV_ARCH.md §5).

=== SDP/Flashloader (SDP=0x1fc9:0x0130, Flashloader=0x15a2:0x0073) ===
  Загрузка Flashloader через SDP (0x1fc9:0x0130)
  Ожидание Flashloader (до 10с).... OK

✅ get_property(CURRENT_VERSION) = [1258424320]

=== configure_memory (FlexSPI NOR) ===
configure_memory: OK

⚠️  Chip erase сотрёт FCB — плата не загрузится до повторной прошивки.
Наберите ERASE для подтверждения: ERASE
  Выставляю timeout=200000мс (эквивалент blhost -t 200000)
flash_erase_all: OK

✅ Гейт 0 (SDP/Flashloader): пройден
```
