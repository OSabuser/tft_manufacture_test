/**
 * @file  sdram.h
 * @brief BSP для внешней SDRAM MT48LC16M16A2 (32 МБ, шина 16 бит).
 *
 * Архитектурное ограничение [DECISION]:
 *   SEMC инициализируется DCD до вызова main(). Этот модуль не трогает
 *   регистры SEMC — только верифицирует работоспособность памяти.
 *
 * Карта памяти:
 *   0x80000000 — начало SDRAM (SEMC BR0)
 *   0x81FFFFFF — конец SDRAM (32 MB)
 *   0x81E00000 — начало non-cacheable региона (USB DMA, 2 MB)
 *
 * Тестовый регион (не пересекается с .data/.bss и non-cacheable):
 *   0x80200000 — начало (2 MB offset от базы)
 *
 * Использование:
 * @code
 *   bsp_sdram_result_t result;
 *
 *   if (bsp_sdram_init() != BSP_OK) { // DCD не отработал
 *       handle_critical_error();
 *   }
 *
 *   bsp_sdram_test_fast(&result);     // ~50 мс, 64 KB
 *   bsp_sdram_test_full(&result);     // ~2–5 с, 1 MB
 * @endcode
 */

#ifndef BSP_SDRAM_H_
#define BSP_SDRAM_H_

#include "bsp/status.h"

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
 * @brief Верифицировать доступность SDRAM.
 *
 * Проверяет что SEMC контроллер инициализирован DCD и SDRAM отвечает —
 * выполняет минимальный write/read/verify на первых 4 байтах тестового
 * региона. Не затрагивает .data/.bss прошивки.
 *
 * @note Не реинициализирует SEMC — DCD уже сделал это до main().
 *
 * @retval BSP_OK           SDRAM доступна и отвечает корректно.
 * @retval BSP_ERR_INIT     SEMC не готов (DCD не отработал).
 * @retval BSP_ERR_TIMEOUT  SEMC занят дольше ожидаемого.
 */
bsp_status_t bsp_sdram_init(void);

#endif /* BSP_SDRAM_H_ */