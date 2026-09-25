# Передача контекста: поддержка платы V3.x (сессия 2026-09-24…25)

> Документ для продолжения работы в новой сессии. Ветка `bsp_tft_v3.2`, база — коммит `6a26562`.
> **Ничего не закоммичено** (см. §6). Язык обсуждения — русский, код и идентификаторы — английский.
> Сборки и host-тесты — в devcontainer (`docker exec -w /workspace tft-devcontainer just …`),
> прогоны на железе делает пользователь.

## 1. Задача и где мы сейчас

Схемотехник выпустил новую ревизию платы индикатора. Схема в `МЮ.Д08.06.10-01_Э3_V3.1.pdf`,
исполнения описаны в `Desc.md`. Цель: BSP под новую плату, host- и HIL-тесты, интеграция
в `firmware_test` и service-tui.

План — [PLAN.md](PLAN.md), фазы 0…6. Сейчас идёт **фаза 0**: углублённое тестирование SDRAM
и FlexSPI на старой и новой платах, обе с SDRAM Micron.

**Текущий шаг:** `hil_sdram_stress` на **старой** плате (эталон). Функциональный набор
по 6 вариантам прошёл 2026-09-25 (§4.6): 5/6 без ошибок, 1 падение — ошибка в самом варианте
`candidate_dqsmd0_132mhz`, не в SDRAM. Вариант исправлен (§5 п. 1). Дальше: на старой плате
перепрогнать его и наборы `in1` и `retention`, затем всё на V3 (§5).

## 2. Документы (все в `docs/hardware/new_board_v3.2/`)

| Файл | Что внутри |
| --- | --- |
| [BOARD_DIFF.md](BOARD_DIFF.md) | Различия старой и новой плат: узлы, разъёмы, распиновка MCU, конфликты (§4), решения (§5) |
| [PLAN.md](PLAN.md) | План по фазам со статусами; справка по режимам строба DQS |
| [DQS_TEST_REPORT.md](DQS_TEST_REPORT.md) | Итоги стресс-теста SDRAM на обеих платах, влияние EXT_IN1, выводы для G0 |
| [SEMC_TIMING.md](SEMC_TIMING.md) | Аудит SEMC/DCD: тайминги против Micron и W9825, нарушение refresh, кандидат, свой DCD (§7) |
| [HW_WATCHDOG.md](HW_WATCHDOG.md) | Отчёт об испытаниях аппаратного сторожа V3 (измерения закрыты) |
| `W9825G6KH.pdf`, схема, `Desc.md` | исходные данные |

## 3. Принятые решения

- **Звук:** I2S, SAI1 → MS4344 → LTK5129. MQS и `VOLUME` на V3 не используются.
- **SDRAM:** целевой чип W9825G6KH-6. Сначала все тесты — на Micron. Рекомендован промышленный
  класс `-6I`: изделие работает при −10…+50 °C, а чипы на схеме рассчитаны на 0…70 °C.
- **QSPI:** W25Q128, как на старой плате. GD25Q128E со схемы не используется.
- **Пины на DQS-падах** (EXT_IN1 на EMC_39 = SEMC_DQS, LCD_DCDC_G на SD_B1_05 = FlexSPI DQS)
  переносим только по итогам фазы 0.
- **DCD:** свой, источник истины — `bsp/generated/dcd/TFT_DCD.mex` (DCD Tool, отдельный .mex).
  Сейчас там импорт legacy `dcd.bin` без 4 записей NIC-301 QoS.
- **LED bootloader** (решение пользователя, код уже изменён): HEARTBEAT **150/350 мс**
  (было 50/450), RECOVERY **200/200 мс** (было 100/100).
- **Порядок тестов:** сначала старая плата как эталон, потом V3.

## 4. Что сделано

### 4.1 Ключевые находки

1. **DQS-пады** (BOARD_DIFF §4). Прошивка стробирует чтение через DQS-пад: SEMC `DQSMD=1`,
   FlexSPI `RXCLKSRC=1` (FCB `w25q128_fdcb.bin`, байт 0x0C). Контроллер выдаёт на пад такт,
   поэтому пад должен висеть в воздухе. На V3 к EMC_39 подключён EXT_IN1, а к SD_B1_05 —
   висящая цепь LCD_DCDC_G.
2. **Refresh в legacy DCD в 2,9 раза медленнее спецификации** (SEMC_TIMING §3): `SDRAMCR3=0x08193D0F`,
   полный цикл 184 мс вместо 64 мс. Кроме того, REF2REF 22 нс при tRFC ≥ 60 нс. Касается и
   **старой производственной платы**. При +50 °C сбои маловероятны, но это нарушение даташита.
3. **У DCD и `bsp_sdram_configure()` разный refresh.** Legacy обнуляет PLL2 PFD0/1/3,
   пишет `CBCDR` целиком (в рантайме это роняет AHB до /4), в проверках не сбрасывает `SEMC_INTR`,
   не ждёт захвата PLL.
4. **W9825 требует 8 auto-refresh при инициализации** (у Micron — 2).
5. **Аппаратный сторож V3** (HW_WATCHDOG.md): таймаут ~30 с, кормит фронт `LED_BLNK` LOW→HIGH
   после LOW ≥ 70 мс, порог 60–70 мс. Старый HEARTBEAT 50/500 **не кормил**, исправлено.
   Сторож работает только при питании от VIN.
   **Риск:** прошивка через USB SDP при питании от VIN — BootROM не мигает, через 30 с передёргивание.
6. **SPSDK 3.7:** имена check-операций для флагов `0x0C`/`0x14` перепутаны относительно RM табл. 9-45.
   Поэтому используем словарь Config Tools и семантику из RM.
7. **Led_2 на V3 = SOUND_KEY** (ключ динамика): `LED_APP` нельзя использовать как индикатор.

### 4.2 Интерпретатор DCD — `bsp/sdram/src/dcd_exec.{c,h}`

- Формат и семантика — RM §9.7.2. Весь DCD валидируется до исполнения, check ограничен таймаутом.
- Публичный API в `bsp/sdram.h`: `bsp_sdram_run_dcd(p_dcd, size)` и отладочная переменная
  `g_bsp_sdram_dcd_offset` — смещение последнего начатого элемента (пары write или заголовка check).
- `dcd_exec_io_t.on_command` — необязательный колбэк трассировки.
- Host-тест `tests/host/dcd_exec` (25 тестов), в том числе прогон реального `tools/host/dcd/dcd.bin`.
- `bsp_sdram_configure()` пока **не тронут**. В фазе 1 он будет исполнять массив из `dcd.c`.

### 4.3 Конвертер — `tools/host/dcd_tool.py`

- Только stdlib. Вход: Config Tools `dcd.c` / `.bin` / `.txt`.
- Выход: `dcd.bin`, C-массив, листинг. Валидирует и предупреждает о записях вне RM табл. 9-42.
- Рецепты `just build::dcd` (→ `build/dcd/`) и `just build::test-tools` (pytest `tools/host/tests`,
  52 теста, в том числе сверка с SPSDK и семантика вариантов на симуляторе регистров).
- Проверена цепочка: `dcd.bin` → импорт в DCD Tool → `dcd.c` → `dcd_tool` = legacy побайтно
  (минус QoS).

### 4.4 Тест аппаратного сторожа (закрыт)

- `tests/target/hil_hw_wdt` + `tools/hil/07_test_hw_wdt.py`
  (`just host::hil-hw-wdt [pytest-args]`, маркер `board_v3`, из `hil-run` исключён).
- Причина сброса определяется по `SRC_SRSR` с чтением и очисткой при старте. `SNVS_LPGPR` не держит
  значение даже без сброса.
- Результаты — HW_WATCHDOG.md. Последние прогоны: 16/16, 15/16 (найден дефект HEARTBEAT),
  6/6 + 1 xfail. После смены паттерна xfail снят, перепрогон `test_real_pattern_feeds` не делался.

### 4.5 Стресс-тест SDRAM — итерация 1 (идёт первый прогон)

- **Прошивка** `tests/target/hil_sdram_stress` (RAM):
  - `INIT <вариант>` → `bsp_sdram_run_dcd()`;
  - `CACHE ON|OFF` — MPU region 8: Normal WB или Device;
  - `RUN DATABUS|ADDRBUS|MARCH <bg>|PRNG <seed>|RETENTION <сек> <seed>` → строка `RESULT`;
  - сторож кормится из `bsp_systick_hook` (250/250);
  - `BOOT` сообщает `semc_clk` и `ren`.
- **Алгоритмы** — `mem_tests.c`, host-тест `tests/host/mem_tests` (9 тестов, неисправности имитируются
  колбэками).
- **Варианты DCD** — `tests/target/hil_sdram_stress/dcd/*.txt` + README, 7 штук: `legacy`, `sdram_c`,
  `candidate`, `candidate_no_refresh`, `candidate_dqsmd0`, `candidate_dqsmd0_132mhz`, `candidate_164mhz`.
  При сборке превращаются в C-массивы через `dcd_tool` (CMake).
- **Хост** — `tools/hil/08_test_sdram_stress.py`, `just host::hil-sdram-stress [pytest-args]`,
  маркер `sdram_stress`, из `hil-run` исключён. 18 тестов в трёх наборах: functional ×7,
  in1 ×6, retention ×4. Перед каждым вариантом снимается VIN, холодный SEMC проверяется по `REN=0`.
  `HIL_BOARD=legacy|v3` — метка в отчёте.
- **Предусловие:** флеш стёрта (`just host::flash-swd-erase`).
- **Исправлено по ходу:**
  1. `BOOT` читал регистры SEMC при выключенном тактировании, и шина висла. Теперь сначала проверяется `CCGR3.CG2`.
  2. `INIT` зависал молча на всех вариантах. Добавлена `diagnose_hang()`: pyOCD останавливает ядро,
     выводит PC/LR/IPSR, CFSR/HFSR/BFAR и элемент DCD по `g_bsp_sdram_dcd_offset`.
     Она показала: DCD не начат, PC в `_exit`, LR рядом с `_free_r`. Причина — `strtok()` из
     newlib-nano выделяет память из кучи, а `_sbrk` в `bsp/generated/syscalls.c` намеренно всегда
     отказывает (кучи в проекте нет), поэтому `abort()`. Заменён на свой `next_token()`.
     **Правило для HIL-прошивок: не использовать функции newlib, которым нужна куча
     (`strtok`, `printf` с float и т. п.).**
- **Итерация 2:** eDMA SDRAM↔SDRAM и фоновое сканирование LCDIF.

### 4.6 Первые результаты стресс-теста — старая плата, Micron (2026-09-25)

Команда: `HIL_BOARD=legacy just host::hil-sdram-stress -k "functional and (legacy or candidate)"`.
Результат: 5 passed, 1 failed, 244 с. Холодный старт подтверждён на каждом варианте (`semc_clk=0`).

| Вариант | SEMC, кГц | Регистры после INIT | Функциональный набор (7 тестов) |
| --- | --- | --- | --- |
| legacy | 135 771 | cr2=`0x00020201` cr3=`0x08193D0F` | ✅ 0 ошибок |
| candidate | 135 771 | cr2=`0x0002090A` cr3=`0x501C0A09` | ✅ 0 ошибок |
| candidate_no_refresh | 135 771 | cr3=`0x501C0A08` (REN=0) | ✅ 0 ошибок (!) |
| candidate_dqsmd0 | 135 771 | mcr=`0x10000000` (DQSMD=0) | ✅ 0 ошибок |
| candidate_164mhz | 163 862 | cr2=`0x00020A0C` cr3=`0x50210A09` | ✅ 0 ошибок |
| candidate_dqsmd0_132mhz | — | — | ❌ `INIT ERR status=2` (TIMEOUT в check) |

Функциональный набор: DATABUS, ADDRBUS, MARCH 00000000 и 55555555 без кэша, PRNG без кэша
и с кэшем, MARCH AAAAAAAA с кэшем. Время на 32 МБ: MARCH без кэша ≈14 с, с кэшем ≈7 с,
PRNG ≈3–4 с.

**Выводы:**
- Прошивка, конвейер DCD и интерпретатор работают на железе. `legacy` (production-DCD) и
  `candidate` чисты.
- **DQSMD=0 на 135,8 МГц на старой плате работает** — важный запасной вариант для EXT_IN1 на EMC_39.
  Проверить на V3.
- **164 МГц работает** — запас по частоте есть.
- `candidate_no_refresh` прошёл весь функциональный набор, то есть ячейки при комнатной
  температуре держат данные как минимум несколько секунд без refresh (MARCH ≈14 с). Точный запас
  покажет набор `retention`.
- **Ошибка в варианте `candidate_dqsmd0_132mhz`:** `PFD2_FRAC = 36`, а по RM (14.8.16) допустимо
  только **12…35**. PFD2 не даёт такта, SEMC стоит, check IPCMDDONE выходит по таймауту.
  132 МГц от PLL2 PFD2 получить нельзя: минимум при FRAC 35 — 271,5 МГц, /2 = 135,8, /3 = 90,5.
  Варианты исправления — §5.
- Попутно: `PFD_528` после BootROM на этой плате = `0x18xx1818` (PFD0/1/3 FRAC=24), а не
  сброс-значение из RM `0x1018101B`. Legacy обнуляет FRAC у PFD0/1/3 (чтение: `0x40634040`).

### 4.6a Старая плата, второй прогон (2026-09-25)

Команда: `HIL_BOARD=legacy just host::hil-sdram-stress -k "(functional and 132mhz) or in1 or retention"`.

- **`candidate_dqsmd0_132mhz` (исправленный):** функциональный набор без ошибок.
- **`in1`:** все 12 без ошибок. **Это только эталон:** на старой плате EXT_IN1 на AD_B1_06, а не
  на DQS-паде. К тому же тест тогда не проверял, доходит ли сигнал M5 до пина. Добавлена сверка (§4.8).
- **`retention`:** legacy, sdram_c, candidate чисты на 1/5/30 с. `candidate_no_refresh`: 1 с — 0,
  5 с — 1 ошибка (DQ7, одна слабая ячейка), 30 с — 189 ошибок, все DQ. Запас удержания самой
  слабой ячейки при комнатной — 1…5 с (норма 64 мс). Грубо (×½ на +10 °C) при +50 °C — 0,2…0,9 с,
  нижняя граница близка к 184 мс у legacy: ещё один довод за кандидат (54 мс) и класс `-6I`/IT.

### 4.8 Сверка проводки EXT_IN1 в `hil_sdram_stress`

- Прошивка: `IN1` → `IN1 ad_b1_06=<0|1> emc_39=<0|1|->`; `IN1 EDGES <мс>` — число фронтов за окно.
  EMC_39 читается, только если DCD отдал пад GPIO (ALT5, варианты `*dqsmd0*`), иначе `-`.
- Хост: `test_in1_wiring` (идёт первым, вариант `candidate_dqsmd0`): M5 выкл/вкл/выкл → пин 0/1/0,
  меандр 5 Гц за 2 с → не меньше половины от ≈20 фронтов. Каждый `test_in1` повторяет проверку уровней
  (для `toggle` — и фронтов), где пин читается. V3 + `candidate` (пад занят SEMC_DQS) опирается на
  `test_in1_wiring`, без него падает. Пин по `HIL_BOARD` (`legacy` → AD_B1_06, `v3` → EMC_39),
  без `HIL_BOARD` набор `in1` падает. В отчёте поле `in1_check=pin|wiring_test`.
- Прошивка собрана, на железе сверка ещё не запускалась.

### 4.7 Прочее

- Паттерны LED bootloader: `led_status.c/.h`, `LED_PATTERNS.md`, `BOOT_FLOW.md`,
  `HARDWARE_VERIFICATION.md`. Сборки Debug и Release чистые.
  **Нужен новый релиз bootloader**, версия пока не поднята.
- `CMakePresets.json`: новые таргеты в списках пресетов (`test_dcd_exec`, `test_mem_tests`,
  `test_hil_hw_wdt`, `test_hil_sdram_stress`). Без этого ctest показывает «Not Run».
  Шаг добавлен в `docs/testing/host/HOST_CREATE_TEST.md`.
- `just host::hil-run` исключает маркеры `board_v3` и `sdram_stress`.

## 5. Следующие шаги

1. ✅ **Исправлен `candidate_dqsmd0_132mhz`** (2026-09-25, ждёт прогона на железе):
   PLL3 PFD1, FRAC 13 → 664,62 МГц, `SEMC_ALT_CLK_SEL=1`, `SEMC_PODF=/5` → **132,92 МГц**.
   DCD включает PLL3 (POWER|ENABLE|EN_USB_CLKS), ждёт LOCK, снимает BYPASS; PLL2 PFD2 не трогает.
   Refresh прежний (`SDRAMCR3=0x501C0A09`): 1,204 мкс × 28 = 33,7 мкс на burst 5 → 55,2 мс на 8K.
   PLL3 PFD1 в рантайме никем не занят (LPSPI — PLL2, LCDIF — PLL5, SAI — PLL3 PFD2/PLL4),
   но `BOARD_BootClockRUN()` ставит ему FRAC 16 (540 МГц → SEMC 108 МГц). Если вариант пойдёт
   в production — поправить clock_config.
   В `dcd_tool` — `pfd_frac_errors()`: FRAC открытого PFD вне 12…35 в итоговом состоянии — ошибка
   (exit 1 и без `--werror`). Обнуление записью значения (legacy PFD0/1/3) не ловится намеренно:
   иначе не собрать production-DCD. `semc_khz` в `hil_sdram_stress` считает и PLL3 PFD1,
   в `REGS`/`INIT OK` добавлен `pfd480`. `just build::test-tools` — 67 passed, `build-hil` чистый.
   Проверка на старой плате: `HIL_BOARD=legacy just host::hil-sdram-stress -k "functional and 132mhz"`.
2. ✅ Полный прогон на обеих платах (2026-09-25) → [DQS_TEST_REPORT.md](DQS_TEST_REPORT.md).
   **Главное:** на V3 при DQSMD=1 включённый EXT_IN1 ломает всё чтение SDRAM (DQ `0xFFFF`,
   ~99 % слов); DQSMD=0 на 135,77 и 132,92 МГц чист на обеих платах при любом IN1.
   **G0 по SEMC (решение пользователя):** приоритет — DQSMD=0 на V3 без переноса EXT_IN1 и без снижения
   частоты; перенос EXT_IN1 — запасной. Совместимый DCD для обеих ревизий не цель.
   NXP (форум): DQSMD=0 допустим на кремнии A1 (у нас `…CVJ5B` = A1), точного предела частоты нет —
   зависит от задержки выхода памяти. Даташит: TIS 8,67 нс (DQSMD=0). SEMC_DQS — только EMC_39 (RM).
   **Запас внутренней петли измерен** (DQS_TEST_REPORT §4a): V3 — край между 158,40 и 163,86 МГц
   (на 163,86 сбоят DQ9/DQ15 — разводка V3), старая плата — чисто и на 163,86. Запас на 135,77 МГц
   ≥ 16,7 % при комнатной. Дальше: лесенка `dqsmd0` при +50/−10 °C, затем повтор на W9825. FlexSPI-часть G0 — `hil_flexspi_stress`.
   Отчёты теперь раздельные: `hil_sdram_stress_report_<HIL_BOARD>.json`.
3. ✅ **FlexSPI (G0, вторая половина)** — прогнан на обеих платах 2026-09-25 (DQS_TEST_REPORT §4b):
   всё чисто, окно DLL при RXCLKSRC=1/133 МГц одинаковое (0…23), LCD_DCDC_G не влияет → FCB не трогаем.
   Soak V3 10 мин — 18,6 ГБ, 0 ошибок. Осталось +50 °C (блокер конца фазы 1). Прежнее описание:
   `HIL_BOARD=legacy just host::hil-flexspi-stress`, затем `HIL_BOARD=v3 …`. Даташит: RXCLKSRC=0 ≤ 60 МГц
   (табл. 37), RXCLKSRC=1 ≤ 133 МГц (табл. 38). Альтернативного пада нет: AD_B1_09 на V3 = SOUND_MCLK.
   Сравнение плат — по окну DLL (OVRDVAL) при RXCLKSRC=1/133 МГц.
   **+50 °C — блокер в конце фазы 1** (PLAN.md, фаза 1 п. 7), −10 °C проверить нечем.
2. Итерация 2 стресс-теста: eDMA и LCDIF. Затем `hil_flexspi_stress` (PLAN 0.2).
3. **Фаза 1:** кандидат → `TFT_DCD.mex` → `dcd.c` → `just build::dcd` → HAB-yaml и bootloader
   на интерпретатор, регресс на старой плате, W9825.
4. Релиз bootloader с новыми паттернами LED. Перепрогон `07_test_hw_wdt.py -k real_pattern`
   с прошитым новым bootloader не обязателен — паттерн проверен эмуляцией.
5. **Фазы 2–6** по PLAN.md. Открытое решение **D1**: одна сборка `firmware_test` на обе ревизии
   (определение платы по BM8563 на LPI2C1) или отдельные бинари.

## 6. Состояние рабочего дерева (не закоммичено)

- **Наши изменения:**
  - `bsp/sdram/*`: `dcd_exec.{c,h}` (новые), `sdram.c/.h`, `CMakeLists.txt`, `README.md`;
  - `tests/host/{dcd_exec,mem_tests}/`, `tests/host/CMakeLists.txt`,
    `tests/target/{hil_hw_wdt,hil_sdram_stress}/`, `tests/target/CMakeLists.txt`;
  - `tools/host/dcd_tool.py`, `tools/host/tests/`, `tools/hil/07_test_hw_wdt.py`,
    `tools/hil/08_test_sdram_stress.py`, `tools/hil/pyproject.toml`;
  - `just/build.just`, `just/host.just`, `CMakePresets.json`;
  - `firmware/bootloader/src/led_status.{c,h}`, `firmware/bootloader/test_stub/HARDWARE_VERIFICATION.md`;
  - `docs/bootloader/{LED_PATTERNS,BOOT_FLOW}.md`, `docs/testing/host/HOST_CREATE_TEST.md`,
    `docs/hardware/new_board_v3.2/*`.
- **Изменения пользователя в Config Tools:**
  - `bsp/generated/dcd/` (`TFT_DCD.mex` + `board/dcd.{c,h}` + `dcd.c.bak`) — оставить.
    `*.bak` стоит добавить в `.gitignore`;
  - `bsp/generated/TFT_Board.mex`, `bsp/generated/board/{clock_config.c,pin_mux.c,pin_mux.h}`,
    новые `board/{dcd.*,peripherals.*}` — побочный эффект первого эксперимента с DCD Tool
    (смена версии data pack, `update_project_code=false` у pins/clocks, тестовая команда `CCM_CBCMR`).
    Предлагалось откатить: `git checkout` плюс удалить новые файлы в `board/`. **Решение за пользователем.**
- В `generated/`: build компилирует `generated/pin_mux.c`, а Config Tools пишет в
  `generated/board/`, и `pin_mux.h` в двух местах уже различаются. Разобраться в фазе 2.
  Весь `generated/` и `TFT_Board.mex` относятся к **старой** плате.

## 7. Открытые вопросы схемотехнику и закупке

1. EXT_IN1 (EMC_39) и LCD_DCDC_G (SD_B1_05) на DQS-падах — после фазы 0, с данными.
2. Назначение LCD_DCDC_G (цепь висит).
3. Сторож: подтвердить задуманные таймаут и импульс; способ отключить на dev-платах;
   поведение при работе от аккумулятора; прошивка через SDP при питании от VIN.
4. SOUND_KEY: какое состояние по умолчанию задумано (сейчас после сброса динамик подключён).
5. LTK5129: вывод SD на GND — рабочий режим?
6. Логика VT10/VT11 (батарейка RTC, `BAT_ON`, `PWR_OFF`).
7. X1.8 теперь BAT_N — совместимость жгутов.
8. Класс SDRAM: W9825G6KH-**6I** / Micron **IT**.
9. X3 (SWD) по схеме DNP. На dev- и HIL-платах нужен. Кабель SWD на стенде переделан 2026-09-25.

## 8. Команды

В devcontainer:

```bash
just build::build-hil
```

```bash
just build::test-host
```

```bash
just build::test-tools
```

```bash
just build::dcd
```

На хосте (стёртая флеш, питание от VIN через M5):

```bash
HIL_BOARD=legacy just host::hil-sdram-stress
```

```bash
just host::hil-hw-wdt
```

Память проекта в Claude: `board-v3-rev.md` (сводка находок и решений по V3).
