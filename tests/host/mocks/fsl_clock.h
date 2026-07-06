#pragma once

/**
 * @file  fsl_clock.h
 * @brief Stub fsl_clock.h для host-тестов.
 *
 * Содержит только то, что реально используется в:
 *   bsp/can/src/can.c — ERRATA 50235 workaround, CLOCK_EnableClock(kCLOCK_Lpuart1)
 *
 * fff предоставляет реализацию через FAKE_VOID_FUNC в тестовом файле.
 *
 * Оригинал: sdk/devices/MIMXRT1052/drivers/fsl_clock.h
 */

/* ── clock_ip_name_t — только используемый член ──────────────────────────── */

typedef enum _clock_ip_name
{
    kCLOCK_Lpuart1 = 0,
} clock_ip_name_t;

/* ── Function signature — реализуется через fff ──────────────────────────── */

void CLOCK_EnableClock(clock_ip_name_t name);