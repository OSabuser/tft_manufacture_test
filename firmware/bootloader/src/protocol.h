/**
 * @file  protocol.h
 * @brief Протокол bootloader — сериализация исходящих событий.
 *
 * Урезанное подмножество протокола firmware_test (firmware/test/src/protocol.h):
 * только то, что нужно для диагностики bootloader через service-tui. Без
 * test_begin/test_result/confirm_request/session_start — те специфичны для
 * тестового раннера firmware_test.
 *
 * Все функции формируют JSON-строку и отправляют через cli_send().
 * Без динамической памяти — каждая функция пишет в стековый буфер.
 *
 * Типы исходящих событий (Фаза 1):
 *   pong             — ответ на {"type":"cmd","cmd":"ping"}
 *   version_response — ответ на {"type":"cmd","cmd":"get_version"}
 *   error            — ошибка протокола или парсинга
 *
 * status-события (waiting_for_sd/installing/smoke_pass/...) добавятся в Фазах 3-4.
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include "version.h"

/** @brief Строка версии bootloader, вставляемая в version_response. */
#define BOOTLOADER_VERSION BOOTLOADER_VERSION_STR

/**
 * @brief Отправить pong — ответ на ping.
 */
void protocol_send_pong(void);

/**
 * @brief Отправить ответ на команду get_version.
 *
 * Формат: {"type":"version_response","fw":"X.Y.Z"}
 */
void protocol_send_version_response(void);

/**
 * @brief Отправить событие error.
 *
 * @param[in] p_code  Короткий ASCII-код ошибки, напр. "PARSE_ERR".
 */
void protocol_send_error(const char *p_code);

#endif /* PROTOCOL_H_ */
