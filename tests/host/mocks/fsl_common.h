#pragma once

/**
 * Stub fsl_common.h для host-тестов.
 * Содержит базовые типы NXP SDK используемые в:
 *   fsl_flexcan.h (stub)
 *
 * В реальном SDK тянет CMSIS device header — на хосте не нужен.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ── status_t ── */

typedef int32_t status_t;

enum
{
    kStatusGroup_Generic = 0,
    kStatusGroup_FLEXCAN = 5,
};

#define MAKE_STATUS(group, code) ((int32_t) ((int32_t) (group) * 100 + (int32_t) (code)))

enum
{
    kStatus_Success = 0,
    kStatus_Fail    = 1,
};