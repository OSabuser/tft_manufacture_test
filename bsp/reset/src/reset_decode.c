/*
 * bsp_reset — разбор маски источников в человекочитаемую причину.
 *
 * ОТДЕЛЬНАЯ единица трансляции от reset.c намеренно: здесь нет ни регистров,
 * ни SDK-заголовков, поэтому файл компилируется на хосте и логика приоритета
 * покрывается тестами (tests/host/reset). В reset.c остаётся только доступ к
 * железу, тестировать в котором нечего.
 */

#include "bsp/reset.h"

#include <stddef.h>

/**
 * @brief Строка причины по одному флагу.
 *
 * Порядок строк = ПРИОРИТЕТ при нескольких взведённых битах, ровно тот же
 * приём, что k_mode_priority[] в домене: правило живёт в порядке данных, а не
 * в коде разбора.
 *
 * Логика порядка: сначала то, что означает СОБЫТИЕ (перегрев, watchdog,
 * зависание/программный сброс), потом отладчик, и только в конце штатные
 * источники. Причина такая: биты SRSR залипают до явной очистки, поэтому
 * «подача питания» может остаться от прошлого цикла и не быть причиной ЭТОГО
 * сброса, а аварийный бит просто так не появляется.
 */
typedef struct
{
    uint32_t flag;
    const char *p_name;
} reset_reason_t;

static const reset_reason_t K_REASONS[] = {
    { BSP_RESET_SRC_OVERHEAT, "перегрев (температурная защита)" },
    { BSP_RESET_SRC_WDOG, "watchdog (WDOG1)" },
    { BSP_RESET_SRC_WDOG3, "watchdog (WDOG3)" },
    { BSP_RESET_SRC_SW_OR_LOCKUP, "программный сброс или зависание ядра" },
    { BSP_RESET_SRC_CSU, "CSU" },
    { BSP_RESET_SRC_JTAG_SW, "отладчик (программный сброс по JTAG)" },
    { BSP_RESET_SRC_JTAG, "отладчик (JTAG HIGH-Z)" },
    { BSP_RESET_SRC_USER_PIN, "внешний сброс (вывод)" },
    { BSP_RESET_SRC_POWER_UP, "подача питания" },
};

#define REASON_COUNT (sizeof(K_REASONS) / sizeof(K_REASONS[0]))

const char *bsp_reset_source_name(uint32_t sources)
{
    for (size_t i = 0U; i < REASON_COUNT; i++)
    {
        if ((sources & K_REASONS[i].flag) != 0U)
        {
            return K_REASONS[i].p_name;
        }
    }

    return "неизвестен";
}
