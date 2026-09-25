# hil_sdram_stress — стресс-тест SDRAM по DCD-вариантам

RAM-прошивка для `tools/hil/08_test_sdram_stress.py` (`just host::hil-sdram-stress`).
План и контекст — `docs/hardware/new_board_v3.2/PLAN.md` (фаза 0), расчёты конфигов —
`SEMC_TIMING.md`, варианты — [dcd/README.md](dcd/README.md).

- Код в ITCM, данные в DTCM/OCRAM — все 32 МБ SDRAM под тест, выборка кода SDRAM не касается.
- SEMC поднимается `bsp_sdram_run_dcd()` из варианта, выбранного командой `INIT <имя>`.
- Кэш SDRAM: `CACHE OFF` — Device (каждое обращение в SDRAM), `CACHE ON` — Normal WB (burst-ы BL8).
- Сторож V3 кормится из `bsp_systick_hook()`: LED_HEARTBEAT 250/250 мс. LED_APP (V3: SOUND_KEY) не трогается.
- Алгоритмы — `mem_tests.c` (host-тест `tests/host/mem_tests`).

Команды — в шапке `main.c`. Предусловие прогона: флеш стёрта (`just host::flash-swd-erase`),
иначе SEMC поднят до INIT и старт не холодный — хост это проверяет по `SDRAMCR3.REN`.
