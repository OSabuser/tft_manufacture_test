/**
 * @file  protocol.h
 * @brief Протокол firmware_test v2 — сериализация исходящих событий.
 *
 * Все функции формируют JSON-строку и отправляют через cli_send().
 * Без динамической памяти — каждая функция пишет в стековый буфер.
 *
 * Типы исходящих событий:
 *   session_start   — при старте (однократно)
 *   test_begin      — перед вызовом run() каждого теста
 *   test_result     — после вызова run()
 *   summary         — после завершения run_all()
 *   confirm_request — интерактивный шаг (ожидание оператора)
 *   pong            — ответ на {"type":"cmd","cmd":"ping"}
 *   error           — ошибка протокола или парсинга
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include "test_module.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Строка версии прошивки, вставляемая в session_start. */
#define FIRMWARE_TEST_VERSION "0.1.4"

/** @brief Таймаут подтверждения по умолчанию, мс. */
#define PROTOCOL_CONFIRM_TIMEOUT_MS 30000U

/**
 * @brief Отправить событие session_start.
 *
 * Вызывается однократно при старте, до приёма первой команды.
 * Пример: {"type":"session_start","fw":"0.1.0","target":"IMXRT1052","uptime_ms":0}
 */
void protocol_send_session_start(void);

/**
 * @brief Отправить событие test_begin.
 *
 * @param[in] p_mod  Дескриптор теста (поля id, name, critical).
 */
void protocol_send_test_begin(const test_module_t *p_mod);

/**
 * @brief Отправить событие test_result.
 *
 * @param[in] p_mod     Дескриптор теста (поле id).
 * @param[in] p_result  Заполненный результат теста.
 */
void protocol_send_test_result(const test_module_t *p_mod, const test_result_t *p_result);

/**
 * @brief Отправить событие summary после завершения run_all().
 *
 * @param[in] passed        Количество пройденных тестов.
 * @param[in] failed        Количество проваленных тестов.
 * @param[in] skipped       Количество пропущенных тестов.
 * @param[in] overall_pass  true если все critical тесты прошли.
 */
void protocol_send_summary(uint8_t passed, uint8_t failed, uint8_t skipped, bool overall_pass);

/**
 * @brief Отправить confirm_request — запрос подтверждения оператора.
 *
 * @param[in] p_params  Параметры подтверждения (id, prompt, timeout_ms).
 */
void protocol_send_confirm_request(const confirm_params_t *p_params);

/**
 * @brief Отправить pong — ответ на ping.
 */
void protocol_send_pong(void);

/**
 * @brief Отправить событие error.
 *
 * @param[in] p_code  Короткий ASCII-код ошибки, напр. "PARSE_ERR".
 */
void protocol_send_error(const char *p_code);

#endif /* PROTOCOL_H_ */