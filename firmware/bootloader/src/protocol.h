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
 *                      installing, update_skipped, recovery_mode,
 *                      smoke_pass/smoke_fail (Фаза 4, SDRAM/SEMC smoke-test,
 *                      см. main.c) — см. protocol_send_status())
 *   wdog             — статус аппаратного watchdog (armed/timeout/recovered,
 *                      см. protocol_send_wdog_status())
 *   qspi_info        — опознанный чип QSPI flash + вписывается ли в минимум
 *                      карты флеша (Фаза 4, см. protocol_send_qspi_info())
 *
 * smoke_pass/smoke_fail и qspi_info шлются один раз сразу после
 * bsp_usb_cdc_init()/bsp_qspi_init() — хост почти наверняка не успевает
 * открыть порт к этому моменту (USB enumeration), cli_send() неблокирующий и
 * теряет запись, если TX ещё не готов. Решение для обоих одинаковое:
 * protocol_set_smoke_result()/protocol_set_qspi_info() кэширует исход,
 * protocol_send_smoke_status()/protocol_send_qspi_info() переспрашивает его в
 * любой момент сессии по командам "smoke_status"/"qspi_info" (тот же приём,
 * что уже был у wdog).
 *
 * Ещё не реализовано (Фаза 4): состояние "booting" и словарь LED-паттернов
 * на LED_APP для всех перечисленных состояний — см. firmware/bootloader/PLAN.md.
 *
 * [DEV-ONLY, Фаза 4] Тип sdram_test — под BOOTLOADER_DEV_DIAGNOSTICS
 * (компилируется только в Debug, см. CMakeLists.txt и dev_sdram_test.h):
 *   sdram_test       — результат одной фазы глубокого теста SDRAM, см.
 *                      protocol_send_sdram_test_phase() и dev_sdram_test.h
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include "version.h"

#include <stdbool.h>
#include <stdint.h>

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
 * @brief Сохранить результат smoke-теста SDRAM/SEMC (Фаза 4) для последующих
 *        protocol_send_smoke_status().
 *
 * Вызывать один раз из main.c сразу после bsp_sdram_configure()+_init().
 *
 * @param[in] pass  true, если оба вызова вернули BSP_OK.
 */
void protocol_set_smoke_result(bool pass);

/**
 * @brief Отправить status с последним сохранённым результатом smoke-теста.
 *
 * Формат: {"type":"status","state":"smoke_pass"} / "smoke_fail". Ничего не
 * делает, если protocol_set_smoke_result() ещё ни разу не вызывался (не
 * должно происходить в штатной последовательности main.c, но на команду
 * "smoke_status" в этом случае лучше промолчать, чем соврать результат).
 */
void protocol_send_smoke_status(void);

/**
 * @brief Сохранить информацию об обнаруженном QSPI flash-чипе (Фаза 4) для
 *        последующих protocol_send_qspi_info().
 *
 * Вызывать один раз из main.c сразу после bsp_qspi_init(). Значения
 * читаются напрямую по JEDEC ID (bsp_qspi_decode_chip()), не зависят от
 * успеха bsp_qspi_init() — так неопознанный/не тот чип тоже репортится с
 * деталями, а не просто "не сработало".
 *
 * @param[in] mfr_id      Сырой manufacturer byte JEDEC ID.
 * @param[in] p_chip_name Имя чипа ("W25Q128", "UNKNOWN") — см. bsp_qspi_decode_chip().
 * @param[in] cap_byte    Сырой capacity byte JEDEC ID.
 * @param[in] size_mb     Обнаруженная ёмкость, МБ (0, если чип не опознан).
 * @param[in] pass        true, если Winbond + известная ёмкость + ёмкость не
 *                        меньше минимума карты флеша (см. main.c).
 */
void protocol_set_qspi_info(
    uint8_t mfr_id, const char *p_chip_name, uint8_t cap_byte, uint32_t size_mb, bool pass);

/**
 * @brief Отправить event с последней сохранённой информацией о QSPI-чипе.
 *
 * Формат: {"type":"qspi_info","chip":"W25Q128","mfr":"0xEF","cap_byte":"0x18",
 *          "size_mb":16,"pass":true}
 *
 * Ничего не делает, если protocol_set_qspi_info() ещё не вызывался.
 */
void protocol_send_qspi_info(void);

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

#ifdef BOOTLOADER_DEV_DIAGNOSTICS
/**
 * @brief [DEV-ONLY] Отправить результат одной фазы dev_sdram_test (Фаза 4).
 *
 * Формат: {"type":"sdram_test","phase":"data_bus","pass":true,"duration_ms":812,
 *          "fail_addr":"0x00000000","expected":"0x00","got":"0x00"}
 *
 * fail_addr/expected/got осмысленны только при pass=false — при pass=true
 * передавать 0/0/0. phase="summary" — итог всего прогона (все фазы пройдены
 * И до summary дошло — см. dev_sdram_test.c).
 *
 * @param[in] p_phase      "configure"/"address_bus"/"data_bus"/"sequential"/
 *                         "retention"/"summary".
 * @param[in] pass         Результат фазы.
 * @param[in] duration_ms  Длительность фазы, мс.
 * @param[in] fail_addr    Адрес первой ошибки (0, если pass=true).
 * @param[in] expected     Ожидаемый байт при ошибке.
 * @param[in] got          Прочитанный байт при ошибке.
 */
void protocol_send_sdram_test_phase(
    const char *p_phase, bool pass, uint32_t duration_ms, uint32_t fail_addr, uint8_t expected, uint8_t got);
#endif /* BOOTLOADER_DEV_DIAGNOSTICS */

#endif /* PROTOCOL_H_ */
