/**
 * @file  cli.h
 * @brief IO-слой CLI для bootloader.
 *
 * Транспорт: USB CDC ACM (bsp_usb_cdc) — единственный канал.
 * Протокол: JSON-lines, каждая строка завершается '\n'. Урезанное
 * подмножество протокола firmware_test (firmware/test/src/cli.h).
 *
 * Входящие типы (Фаза 1):
 *   {"type":"cmd", "cmd":"ping"}
 *   {"type":"cmd", "cmd":"get_version"}
 *
 * Исходящие события формируются через protocol.h, а не напрямую через cli_send().
 * cli_send() остаётся публичным: его использует protocol.c как единственную
 * точку вывода.
 */

#ifndef CLI_H_
#define CLI_H_

#include <stddef.h>

/** @brief Максимальная длина входящей JSON-строки включая '\n'. */
#define CLI_LINE_BUF_SIZE 128U

/**
 * @brief Инициализировать CLI. Сбрасывает внутренний буфер строки.
 *
 * Вызывать после bsp_usb_cdc_init() и до первого cli_process().
 */
void cli_init(void);

/**
 * @brief Отправить готовую JSON-строку через USB CDC.
 *
 * @param[in] p_resp  NUL-terminated строка, завершённая '\n'.
 *
 * @note Неблокирующий. Если TX занят — запись теряется.
 */
void cli_send(const char *p_resp);

/**
 * @brief Обработать входящие байты, диспатчить сообщение при получении '\n'.
 *
 * Вызывать в главном цикле после bsp_usb_cdc_poll().
 * Неблокирующий: если данных нет — возвращается немедленно.
 */
void cli_process(void);

#endif /* CLI_H_ */
