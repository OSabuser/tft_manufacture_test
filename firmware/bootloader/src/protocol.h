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
 * Типы исходящих событий (Фаза 3):
 *   status           — top-level состояние bootloader (waiting_for_sd,
 *                      installing, update_skipped, recovery_mode — см.
 *                      protocol_send_status())
 *   wdog             — статус аппаратного watchdog (armed/timeout/recovered,
 *                      см. protocol_send_wdog_status())
 *
 * Полный словарь состояний status (smoke_pass/smoke_fail/booting/...)
 * появится в Фазе 4.
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

/**
 * @brief Отправить событие status — top-level состояние bootloader.
 *
 * Формат: {"type":"status","state":"waiting_for_sd"}
 *
 * @param[in] p_state  Короткий ASCII-идентификатор состояния,
 *                     напр. "waiting_for_sd", "installing".
 */
void protocol_send_status(const char *p_state);

/**
 * @brief Отправить статус аппаратного watchdog и счётчика попыток загрузки
 *        (Фаза 6).
 *
 * Формат: {"type":"wdog","armed":true,"timeout_s":10,"recovered":false,
 *          "reset_count":0,"threshold":3}
 *  - armed       — watchdog взведён (bsp_wdog_init выполнен);
 *  - timeout_s   — сконфигурированный таймаут в секундах;
 *  - recovered   — ПОСЛЕДНИЙ сброс МК был по таймауту watchdog (плата
 *                  восстановилась после зависания);
 *  - reset_count — bsp_boot_attempt_count(): сколько попыток подряд без
 *                  подтверждения здоровья (health-mark/новая установка), 0
 *                  сразу после POR;
 *  - threshold   — RECOVERY_DEFAULT_THRESHOLD: порог фолбэка/recovery.
 *
 * Эмитится один раз на старте, если recovered, и по команде "wdog".
 */
void protocol_send_wdog_status(void);

#endif /* PROTOCOL_H_ */
