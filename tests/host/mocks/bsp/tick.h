#pragma once

/**
 * Stub bsp/tick.h для host-тестов.
 * bsp_tick_get_ms() мокается через fff в тест-файлах где нужен контроль времени.
 */

#include <stdint.h>

uint32_t bsp_tick_get_ms(void);
void bsp_delay(uint32_t ms);