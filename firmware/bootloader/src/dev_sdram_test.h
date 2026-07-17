/**
 * @file  dev_sdram_test.h
 * @brief [DEV-ONLY] Глубокий тест SDRAM через bsp_sdram_configure() (Фаза 4).
 *
 * НЕ для production — компилируется только в Debug
 * (BOOTLOADER_DEV_DIAGNOSTICS, см. CMakeLists.txt); отсутствует в HAB
 * Release-бинаре.
 *
 * Цель — не заменить лёгкий boot-time smoke-test (main.c, "smoke_pass"/
 * "smoke_fail"), а дать ту же глубину проверки, что уже доверена
 * firmware_test/src/tests/test_sdram.c (4 фазы, покрывают адресную шину,
 * шину данных, coupling между ячейками и refresh-timing), но против нового
 * C-порта DCD (bsp_sdram_configure()), а не против DCD напрямую — чтобы
 * убедиться, что порт поднимает память так же надёжно, как проверенный
 * годами в производстве DCD.
 *
 * Вызывается по CDC-команде {"type":"cmd","cmd":"sdram_test"} (см. cli.c).
 * Блокирует главный цикл на время прогона (~4 с, см. таймингы ниже — реально
 * измерено на железе, а не оценка) — сама кормит watchdog и опрашивает CDC по
 * ходу, вызывающему коду ничего дополнительно делать не нужно. Каждая фаза и
 * итог репортятся отдельным событием по CDC синхронно по ходу прогона (см.
 * protocol_send_sdram_test_phase()).
 */
#ifndef DEV_SDRAM_TEST_H_
#define DEV_SDRAM_TEST_H_

/**
 * @brief Поднять SEMC (bsp_sdram_configure()) и прогнать 4-фазный тест SDRAM.
 *
 * Фазы (портированы из firmware/test/src/tests/test_sdram.c без изменений
 * логики — меняется только то, кто поднимает SEMC до них). Тайминги —
 * реальный прогон на железе (не оценка из test_sdram.c, та оказалась
 * консервативнее раз в 6 — запись садится в D-Cache почти мгновенно, реальная
 * задержка SDRAM только на flush_dcache() и на чтение при верификации):
 *   1. address_bus — 24 адресных бита (13 row + 9 col + 2 bank), <1 мс.
 *   2. data_bus    — walking ones + инверсия, 64 KB, ~130 мс.
 *   3. sequential  — address pattern + инверсия, 2 MB, ~3.7 с.
 *   4. retention   — запись → flush → 200 мс → verify, 256 KB, ~400 мс.
 *
 * Если bsp_sdram_configure()/bsp_sdram_init() не прошли — репортится фаза
 * "configure" с pass=false, дальнейшие фазы не запускаются.
 */
void dev_sdram_test_run(void);

#endif /* DEV_SDRAM_TEST_H_ */
