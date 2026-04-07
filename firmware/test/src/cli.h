/**
 * @file  cli.h
 * @brief Command-line interface для firmware_test.
 *
 * Единственный транспорт — USB CDC ACM (bsp_usb_cdc).
 * Протокол — JSON-lines: каждая строка завершается '\n'.
 * Запрос: {"cmd":"NAME"}\n
 * Ответ:  {"ok":true,...}\n  или  {"ok":false,"error":"CODE"}\n
 *
 * @note Архитектурное ограничение: этот модуль жёстко связан с
 *       bsp_usb_cdc как единственным IO-каналом. Замена транспорта
 *       не предусмотрена — firmware_test работает только через USB CDC.
 *       Для HIL ELF-прошивок используется отдельный канал связи (UART + bsp_uart_host).
 */

#ifndef CLI_H_
#define CLI_H_

#include <stddef.h>

/** @brief Максимальная длина входящей JSON-строки включая завершающий '\n'. */
#define CLI_LINE_BUF_SIZE 128U

/**
 * @brief Инициализировать CLI.
 *
 * Сбрасывает внутренний буфер строки.
 * Вызывать после bsp_usb_cdc_init() и до первого cli_process().
 */
void cli_init(void);

/**
 * @brief Отправить готовую JSON-строку через USB CDC.
 *
 * @param[in] resp  NUL-terminated строка, завершённая '\n'.
 *
 * @note Неблокирующий вызов. Если TX занят — запись теряется.
 *       Для firmware_test это приемлемо: хост повторит запрос.
 */
void cli_send(const char *resp);

/**
 * @brief Обработать входящие байты и диспатчить команду при получении '\n'.
 *
 * Вызывать в главном цикле после bsp_usb_cdc_poll().
 * Неблокирующий: если данных нет — возвращается немедленно.
 */
void cli_process(void);

#endif /* CLI_H_ */