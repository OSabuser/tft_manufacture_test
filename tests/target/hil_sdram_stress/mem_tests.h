/**
 * @file  mem_tests.h
 * @brief Алгоритмы проверки внешней памяти для hil_sdram_stress.
 *
 * Платформо-независимы: работают по указателю на 32-битные слова, синхронизацию
 * кэша и «признаки жизни» (UART keepalive) получают через колбэки. Поэтому
 * проверяются host-тестом (tests/host/mem_tests) на обычном буфере.
 *
 * Отчёт об ошибках — mem_result_t: число ошибок, первая ошибка (смещение,
 * ожидалось/прочитано) и OR всех расхождений. SDRAM 16-битная, 32-битное
 * слово — две колонки по 16 бит на одних и тех же линиях DQ0..15, поэтому
 * mem_dq_mask() сворачивает расхождения в маску линий DQ: случайные биты по
 * всем линиям — тайминги/строб, фиксированные линии — монтаж.
 */

#ifndef MEM_TESTS_H_
#define MEM_TESTS_H_

#include <stddef.h>
#include <stdint.h>

typedef struct mem_result_s
{
    uint32_t errors;       /**< Число несовпавших слов (насыщается на UINT32_MAX). */
    uint32_t first_offset; /**< Смещение первой ошибки, байт от базы. */
    uint32_t expected;     /**< Ожидалось в первой ошибке. */
    uint32_t actual;       /**< Прочитано в первой ошибке. */
    uint32_t diff_or;      /**< OR (expected ^ actual) по всем ошибкам. */
} mem_result_t;

typedef struct mem_hooks_s
{
    /** Перед чтением-проверкой: сбросить кэш в память и инвалидировать. NULL — не нужно. */
    void (*sync)(void);
    /** Вызывается примерно каждые MEM_PROGRESS_WORDS слов. NULL — не нужно. */
    void (*progress)(void);
} mem_hooks_t;

#ifndef MEM_PROGRESS_WORDS
#define MEM_PROGRESS_WORDS (1UL << 20) /* степень двойки; host-тест переопределяет */
#endif

/** Маска линий DQ0..15, на которых были расхождения. */
uint16_t mem_dq_mask(const mem_result_t *p_res);

void mem_result_clear(mem_result_t *p_res);

/** Бегущие 1 и 0 по 32 битам в одном слове — обрывы/замыкания линий данных. */
void mem_test_databus(volatile uint32_t *p_word, mem_result_t *p_res);

/**
 * Адресная шина: смещения-степени двойки (слова). Ловит залипшие и
 * замкнутые адресные линии (запись по одному адресу видна по другому).
 */
void mem_test_addrbus(volatile uint32_t *p_base, size_t words, const mem_hooks_t *p_hooks,
                      mem_result_t *p_res);

/**
 * March C−: ⇑w0; ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕r0,
 * где 0 = background, 1 = ~background.
 */
void mem_test_march_c(volatile uint32_t *p_base, size_t words, uint32_t background,
                      const mem_hooks_t *p_hooks, mem_result_t *p_res);

/** Заполнить псевдослучайной последовательностью (xorshift32 от seed). */
void mem_prng_fill(volatile uint32_t *p_base, size_t words, uint32_t seed,
                   const mem_hooks_t *p_hooks);

/** Проверить последовательность, записанную mem_prng_fill() с тем же seed. */
void mem_prng_verify(volatile uint32_t *p_base, size_t words, uint32_t seed,
                     const mem_hooks_t *p_hooks, mem_result_t *p_res);

#endif /* MEM_TESTS_H_ */
