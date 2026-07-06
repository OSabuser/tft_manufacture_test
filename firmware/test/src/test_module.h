/**
 * @file  test_module.h
 * @brief Интерфейс тест-модуля для firmware_test.
 *
 * Каждый тест периферии реализует этот интерфейс и регистрируется
 * в реестре test_runner. Добавление нового теста — одна строка в реестре,
 * без изменений в CLI или протокольном слое.
 */

#ifndef TEST_MODULE_H_
#define TEST_MODULE_H_

#include <stdbool.h>
#include <stdint.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Размер буфера детального описания результата (включая NUL). */
#define TEST_DETAIL_SIZE 96U

/** @brief Максимальная длина идентификатора теста (включая NUL). */
#define TEST_ID_MAX_SIZE 24U

/**
 * @brief Верхняя граница числа тест-модулей в системе.
 *
 * Общий "потолок" для двух независимых мест:
 *  - test_runner.c: compile-time ёмкость g_s_selected[] (маска выбора
 *    для RUNNER_MODE_SELECTED). REGISTRY_SIZE в UNIT_TEST-сборке —
 *    runtime-значение (g_unit_test_registry_size), не может быть
 *    границей массива со static storage duration.
 *  - cli.c: максимальный размер входящего массива "tests" в команде
 *    run_selected (handle_cmd_run_selected()).
 *
 * Реальный реестр (non-UNIT_TEST, k_registry) — 8 модулей, запас есть.
 * При добавлении нового теста, если REGISTRY_SIZE превысит эту границу,
 * сборка упадёт на _Static_assert в test_runner.c — увеличивать здесь.
 */
#define TEST_REGISTRY_MAX_SIZE 16U

/* ── Типы результата ───────────────────────────────────────────────────── */

/**
 * @brief Итог выполнения теста.
 */
typedef enum
{
    TEST_STATUS_PASS = 0, /**< Тест пройден. */
    TEST_STATUS_FAIL,     /**< Тест провален. */
    TEST_STATUS_SKIP, /**< Тест пропущен (нет оборудования, отказ оператора). */
} test_status_t;

/**
 * @brief Результат, возвращаемый из run().
 *
 * @note Поле duration_ms заполняет test_runner, не сам модуль.
 */
typedef struct
{
    test_status_t status;          /**< Итог теста. */
    uint32_t duration_ms;          /**< Время выполнения, мс. */
    char detail[TEST_DETAIL_SIZE]; /**< Описание ошибки или пустая строка. */
} test_result_t;

/* ── Параметры подтверждения ───────────────────────────────────────────── */

/**
 * @brief Параметры запроса оператора (confirm_request).
 *
 * Используется test_runner_request_confirm() для интерактивных шагов теста.
 */
typedef struct
{
    const char *id;     /**< Идентификатор подтверждения. */
    const char *prompt; /**< Инструкция оператору. */
    uint32_t timeout_ms; /**< Таймаут ожидания в мс. 0 — использовать дефолт. */
} confirm_params_t;

/* ── Дескриптор модуля ─────────────────────────────────────────────────── */

/**
 * @brief Дескриптор одного тест-модуля.
 *
 * Все функциональные поля, кроме run(), могут быть NULL:
 * - init()   : однократная инициализация перед run(). Может быть NULL.
 * - run()    : выполняет тест. Не может быть NULL. Интерактивные шаги
 *              реализуются через test_runner_request_confirm().
 * - deinit() : освобождение ресурсов после run(), вызывается даже при FAIL.
 *              Может быть NULL.
 *
 * @note Долгие run() ДОЛЖНЫ периодически вызывать bsp_usb_cdc_poll(),
 *       чтобы USB стек оставался живым.
 */
typedef struct
{
    const char *id;    /**< Короткий ASCII-ключ, напр. "sdram". */
    const char *name;  /**< Читаемое имя, напр. "SDRAM 32 MB". */
    bool critical;     /**< true → run_all() останавливается при FAIL. */
    bool requires_hil; /**< true → нужен M5StampPLC. */
    const char *pre_confirm_prompt; /**< Запрос оператору до запуска, или NULL. */
    void (*init)(void);
    test_result_t (*run)(void);
    void (*deinit)(void);
} test_module_t;

#endif /* TEST_MODULE_H_ */