/**
 * @file  boot_select.h
 * @brief Выбор и запуск образа tft_app через bootutil (Direct-XIP).
 */

#ifndef BOOT_SELECT_H_
#define BOOT_SELECT_H_

/**
 * @brief Выбрать образ (bootutil boot_go, Direct-XIP) и прыгнуть в него.
 *
 * При успехе не возвращается — управление переходит в выбранный образ.
 * При провале (нет валидного образа ни в одном слоте, или оба слота стёрты
 * bootutil'ом из-за незавершённого revert) — возвращается, чтобы main.c
 * мог продолжить в ping/pong-цикл (задел на состояние "жду SD" Фазы 3).
 *
 * @pre bsp_qspi_init() уже вызван.
 */
void boot_select_and_jump(void);

#endif /* BOOT_SELECT_H_ */
