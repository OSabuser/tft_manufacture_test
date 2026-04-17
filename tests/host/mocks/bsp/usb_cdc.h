/**
 * @file  bsp/usb_cdc.h
 * @brief Stub для host-тестов. Функции мокируются через fff.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

void bsp_usb_cdc_poll(void);
size_t bsp_usb_cdc_read(uint8_t *p_buf, size_t size);
void bsp_usb_cdc_write(const uint8_t *p_data, size_t size);