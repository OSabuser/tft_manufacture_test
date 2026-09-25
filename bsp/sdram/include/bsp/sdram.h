/**
 * @file  sdram.h
 * @brief BSP для внешней SDRAM MT48LC16M16A2 (32 МБ, шина 16 бит).
 *
 * Два пути в зависимости от того, кто использует SEMC:
 *
 *   - Прошивки С DCD (firmware_test): SEMC поднят DCD до main().
 *     bsp_sdram_init() только верифицирует доступность памяти.
 *
 *   - Прошивки БЕЗ DCD (bootloader smoke-test; в будущем — tft_app под свой
 *     XIP): bsp_sdram_configure() сам поднимает SEMC (порт проверенной
 *     DCD-последовательности в C, см. tools/host/dcd/dcd.bin), затем
 *     bsp_sdram_init() верифицирует, как и в первом случае.
 *
 * Карта памяти:
 *   0x80000000 — начало SDRAM (SEMC BR0)
 *   0x81FFFFFF — конец SDRAM (32 MB)
 *   0x81E00000 — начало non-cacheable региона (USB DMA, 2 MB)
 *
 * Тестовый регион (не пересекается с .data/.bss и non-cacheable):
 *   0x80200000 — начало (2 MB offset от базы)
 *
 * Использование (путь без DCD):
 * @code
 *   if (bsp_sdram_configure() != BSP_OK) { handle_semc_bringup_error(); }
 *   if (bsp_sdram_init()      != BSP_OK) { handle_sdram_error(); }
 * @endcode
 */

#ifndef BSP_SDRAM_H_
#define BSP_SDRAM_H_

#include "bsp/status.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Размер расширенного тестового региона, байт (27 MB).
 *
 * От BSP_SDRAM_TEST_BASE_ADDR (0x80200000) до 0x81D00000.
 * Оставляет 1 MB защитную зону перед heap и стеком firmware_test
 * (stack top = 0x81E00000 − 4KB heap − 4KB stack ≈ 0x81DFE000).
 */
#define BSP_SDRAM_TEST_EXTENDED_SIZE 0x01B00000UL

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Базовый адрес SDRAM (SEMC BR0). */
#define BSP_SDRAM_BASE_ADDR 0x80000000UL

/** @brief Полный размер SDRAM, байт (32 MB). */
#define BSP_SDRAM_SIZE_BYTES 0x02000000UL

/**
 * @brief Базовый адрес тестового региона.
 *
 * Смещение 2 MB от базы — гарантированно выше .data/.bss firmware_test
 * и ниже non-cacheable региона (0x81E00000).
 */
#define BSP_SDRAM_TEST_BASE_ADDR 0x80200000UL

/** @brief Размер быстрого теста, байт (64 KB). */
#define BSP_SDRAM_TEST_FAST_SIZE 0x00010000UL

/** @brief Размер полного теста, байт (1 MB). */
#define BSP_SDRAM_TEST_FULL_SIZE 0x00100000UL

/* ── Public API ────────────────────────────────────────────────────────── */

/**
 * @brief Поднять SEMC и инициализировать внешнюю SDRAM (для вызывателей без DCD).
 *
 * Побитово-точный порт проверенной в производстве DCD-последовательности
 * (tools/host/dcd/dcd.bin, «блок 2») в вызываемый C-код:
 *   1. тактирование SEMC (PLL2 528 МГц → PFD2 271.54 МГц → ÷2 = 135.77 МГц);
 *   2. IOMUX/PAD пинов GPIO_EMC (функция SEMC, DQS с SION);
 *   3. регистры контроллера SEMC (MCR/BR/IOCR/SDRAMCR0..3/…), значения из DCD;
 *   4. командная последовательность SDRAM: precharge-all → 2×auto-refresh →
 *      mode-set → включение авто-refresh;
 *   5. AXI-QoS приоритеты SDRAM-мастеров (LCD/Cortex-M7) — NIC-301 GPV,
 *      отдельный IP вне карты SEMC (i.MX RT1050 RM, гл. 29).
 *
 * Прошивки С DCD (firmware_test) получают то же самое до main() и эту функцию
 * НЕ вызывают — только bsp_sdram_init().
 *
 * @note Безопасно вызывать в рантайме после board_hw_init(): CPU тактируется от
 *       ARM PLL, FlexSPI-XIP — от USB1 PLL; PLL2/PFD2 поднимаются с нуля и не
 *       задевают ни то, ни другое (под SKIP_SYSCLK_INIT штатный
 *       BOARD_BootClockRUN() эту цепочку не трогает).
 *
 * @note Требует MPU Region 11 (board_mpu_init(), bsp/generated/board.c) —
 *       шаг 5 пишет NIC-301 GPV (0x41000000+), который Region 10
 *       (периферия, только 4 МБ от 0x40000000) не покрывает; без Region 11
 *       запись фолтит (deny-all errata-регион 0 перехватывает всё
 *       непокрытое). Уже добавлен — заметка для будущих правок MPU-таблицы.
 *
 * @retval BSP_OK        SEMC поднят, SDRAM инициализирована.
 * @retval BSP_ERR_INIT  Командная последовательность SDRAM не завершилась
 *                       (IP-команда SEMC вернула ошибку).
 */
bsp_status_t bsp_sdram_configure(void);

/**
 * @brief Исполнить DCD-массив в рантайме — то же, что делает BootROM до main().
 *
 * Формат и семантика — i.MX RT1050 RM Rev.4 §9.7.2 (write/check/NOP).
 * Массив целиком валидируется до исполнения первой команды. Check-команды
 * опрашиваются не дольше 10 мс каждая (у BootROM без count — бесконечно).
 *
 * Назначение: один DCD-источник (DCD Tool, Config Tools) для BootROM и для
 * вызывателей без DCD (bootloader, HIL-стресс-тесты SDRAM с вариантами конфига).
 *
 * @note Те же ограничения среды, что у bsp_sdram_configure(): если DCD трогает
 *       тактирование SEMC или NIC-301 GPV (0x41000000+) — нужны
 *       SKIP_SYSCLK_INIT и MPU Region 11.
 *
 * @param p_dcd  DCD-массив (заголовок 0xD2 + команды).
 * @param size   Размер массива, байт.
 *
 * @retval BSP_OK                Все команды исполнены.
 * @retval BSP_ERR_PARAM         p_dcd == NULL.
 * @retval BSP_ERR_INVALID       Битый DCD — ни одна команда не исполнена.
 * @retval BSP_ERR_NOT_SUPPORTED Неизвестная команда — ни одна не исполнена.
 * @retval BSP_ERR_TIMEOUT       Check-команда не дождалась условия.
 */
bsp_status_t bsp_sdram_run_dcd(const uint8_t *p_dcd, size_t size);

/**
 * @brief Отладка: смещение последнего НАЧАТОГО элемента DCD (пары write или
 *        заголовка check/NOP) в
 *        bsp_sdram_run_dcd() (0xFFFFFFFF — ни одной). Если исполнение зависло
 *        на обращении к регистру, отладчик читает эту переменную и видит, на
 *        какой команде (tools/hil/08_test_sdram_stress.py делает это сам).
 */
extern volatile uint32_t g_bsp_sdram_dcd_offset;

/**
 * @brief Верифицировать доступность SDRAM.
 *
 * Проверяет что SEMC контроллер инициализирован (DCD или bsp_sdram_configure())
 * и SDRAM отвечает — выполняет минимальный write/read/verify на первых 4 байтах
 * тестового региона. Не затрагивает .data/.bss прошивки.
 *
 * @note Не (ре)инициализирует SEMC — предполагает, что DCD либо
 *       bsp_sdram_configure() уже это сделали.
 *
 * @retval BSP_OK           SDRAM доступна и отвечает корректно.
 * @retval BSP_ERR_INIT     SEMC не готов (инициализация не отработала).
 * @retval BSP_ERR_TIMEOUT  SEMC занят дольше ожидаемого.
 */
bsp_status_t bsp_sdram_init(void);

#endif /* BSP_SDRAM_H_ */
