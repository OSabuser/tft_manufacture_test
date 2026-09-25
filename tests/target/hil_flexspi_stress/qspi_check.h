/**
 * @file  qspi_check.h
 * @brief Платформо-независимая часть hil_flexspi_stress: PRNG-паттерн, сверка,
 *        маска линий IO и план тактирования FlexSPI.
 *
 * Проверяется host-тестом tests/host/qspi_check. Регистров не трогает.
 *
 * Quad I/O: каждый байт идёт двумя тактами (старший полубайт, затем младший),
 * бит k полубайта — линия IOk. Поэтому qspi_io_mask() сворачивает OR расхождений
 * по всем полубайтам в маску IO0..3: одна линия — монтаж или разводка, все —
 * момент выборки (строб).
 */

#ifndef QSPI_CHECK_H_
#define QSPI_CHECK_H_

#include <stddef.h>
#include <stdint.h>

typedef struct qspi_result_s
{
    uint32_t errors;       /**< Число несовпавших слов (насыщается на UINT32_MAX). */
    uint32_t first_offset; /**< Смещение первой ошибки, байт от начала области. */
    uint32_t expected;     /**< Ожидалось в первой ошибке. */
    uint32_t actual;       /**< Прочитано в первой ошибке. */
    uint32_t diff_or;      /**< OR (expected ^ actual) по всем ошибкам. */
} qspi_result_t;

/**
 * Такт FlexSPI: PLL3 (480 МГц) → PFD0 (480·18/FRAC) → FLEXSPI_PODF → serial root.
 * В SDR serial root = SCK.
 */
typedef struct qspi_clk_plan_s
{
    uint16_t mhz;      /**< Номинал для CLI («133», «60»…). */
    uint8_t pfd0_frac; /**< PLL3 PFD0 FRAC, 12…35. */
    uint8_t podf;      /**< Делитель FLEXSPI_PODF, 1…8. */
} qspi_clk_plan_t;

void qspi_result_clear(qspi_result_t *p_res);

/** xorshift32; состояние не может быть 0 (seed 0 заменяется на 1). */
uint32_t qspi_prng_seed(uint32_t seed);
uint32_t qspi_prng_next(uint32_t *p_state);

/** Заполнить буфер продолжением последовательности. */
void qspi_prng_fill(uint32_t *p_dst, size_t words, uint32_t *p_state);

/**
 * Сверить буфер с продолжением последовательности.
 * @param offset смещение p_words от начала области, байт (для first_offset).
 */
void qspi_verify(const volatile uint32_t *p_words, size_t words, uint32_t *p_state, uint32_t offset,
                 qspi_result_t *p_res);

/** Маска линий IO0..3 с расхождениями. */
uint8_t qspi_io_mask(uint32_t diff_or);

/** План тактирования по номиналу в МГц; NULL — нет такого. */
const qspi_clk_plan_t *qspi_clk_plan_find(uint32_t mhz);

/** Фактическая частота serial root, кГц. */
uint32_t qspi_clk_khz(const qspi_clk_plan_t *p_plan);

#endif /* QSPI_CHECK_H_ */
