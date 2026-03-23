#pragma once

/**
 * Stub fsl_iomuxc.h для host-тестов.
 * В реальном SDK содержит только inline-функции для настройки pin mux.
 * В opto.c используется как транзитивный include — прямых вызовов нет,
 * поэтому stub пустой.
 */