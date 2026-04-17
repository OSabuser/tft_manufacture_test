/**
 * @file  test_runner.h
 * @brief Реестр тест-модулей и state machine запуска для firmware_test.
 *
 * Добавление нового теста — одна строка в реестре test_runner.c.
 * Никаких изменений в CLI или протокольном слое не требуется.
 *
 * Архитектурные ограничения:
 *  - Без динамической памяти.
 *  - run() тест-модуля блокирует вызывающий контекст.
 *  - Долгие run() обязаны вызывать bsp_usb_cdc_poll() внутри.
 */

#ifndef TEST_RUNNER_H_
#define TEST_RUNNER_H_

#include "test_module.h"

#include <stdbool.h>

/**
 * @brief Инициализировать runner. Сбрасывает состояние в IDLE и обнуляет счётчики.
 *
 * Вызывать после cli_init(), до первого cli_process().
 */
void test_runner_init(void);

/**
 * @brief Обработать текущее состояние runner в главном цикле.
 *
 * Неблокирующий. Обслуживает ожидание confirm в состоянии PRE_CONFIRM.
 * Вызывать в каждой итерации главного цикла после cli_process().
 */
void test_runner_process(void);

/**
 * @brief Запустить один тест по идентификатору.
 *
 * Если runner занят — отправляет {"ok":false,"error":"BUSY"}.
 * Если id не найден — отправляет {"ok":false,"error":"UNKNOWN_TEST"}.
 *
 * @param[in] p_id  ASCII-идентификатор теста, напр. "sdram".
 */
void test_runner_run_single(const char *p_id);

/**
 * @brief Запустить все тесты из реестра по порядку.
 *
 * Если runner занят — отправляет {"ok":false,"error":"BUSY"}.
 * После завершения всех тестов отправляет protocol_send_summary().
 * Critical fail останавливает выполнение: оставшиеся тесты получают SKIP.
 */
void test_runner_run_all(void);

/**
 * @brief Принять ответ оператора на confirm_request.
 *
 * Вызывается из cli.c при получении {"type":"confirm","id":"...","confirmed":...}.
 * Если id не совпадает с ожидаемым — игнорируется (stale confirm).
 *
 * @param[in] p_id      Идентификатор подтверждения из поля "id".
 * @param[in] confirmed true если оператор подтвердил.
 */
void test_runner_on_confirm(const char *p_id, bool confirmed);

/**
 * @brief Проверить, занят ли runner.
 *
 * @return true если runner не в IDLE — cli.c должен отвечать BUSY.
 */
bool test_runner_is_busy(void);

/**
 * @brief Отправить confirm_request и заблокироваться до ответа оператора или таймаута.
 *
 * Предназначен для вызова из run() тест-модуля (display, интерактивные шаги).
 * Внутри polling loop вызывает bsp_usb_cdc_poll() + cli_process() — USB-стек
 * остаётся живым и confirm может быть принят без возврата в главный цикл.
 *
 * @note Не использовать для теста кнопок: там подтверждение — физическое нажатие,
 *       а не JSON. Кнопочный тест вызывает protocol_send_confirm_request() напрямую,
 *       затем поллит bsp_button сам.
 *
 * @param[in] p_params  Параметры запроса (id, prompt, timeout_ms).
 * @return true если оператор подтвердил, false при отказе или таймауте.
 */
bool test_runner_wait_confirm(const confirm_params_t *p_params);

#endif /* TEST_RUNNER_H_ */