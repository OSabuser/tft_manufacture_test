#pragma once

/**
 * Stub board.h для host-тестов.
 * Содержит только сигнатуры используемые в bsp-модулях.
 * fff предоставляет реализации в тест-файлах где нужно.
 */

void BOARD_InitBootPins(void);
void BOARD_InitPins(void);
void BOARD_InitRS_UART(void);
void BOARD_InitRS_GPIO(void);
void board_hw_init(void);