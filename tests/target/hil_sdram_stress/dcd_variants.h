/**
 * @file  dcd_variants.h
 * @brief Таблица DCD-вариантов hil_sdram_stress (генерируется CMake из dcd/*.txt).
 */

#ifndef DCD_VARIANTS_H_
#define DCD_VARIANTS_H_

#include <stddef.h>
#include <stdint.h>

typedef struct dcd_variant_s
{
    const char *name;     /**< Имя файла без .txt. */
    const uint8_t *data;  /**< DCD-массив (tools/host/dcd_tool.py --c). */
    const size_t *p_size; /**< Размер массива, байт. */
} dcd_variant_t;

extern const dcd_variant_t g_dcd_variants[];
extern const size_t g_dcd_variant_count;

#endif /* DCD_VARIANTS_H_ */
