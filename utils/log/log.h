/**
 * @file  log.h
 * @brief Платформонезависимый логгер с callback-транспортом.
 *
 * Ядро логгера не знает о конкретном транспорте — UART, Flash, USB CDC
 * и т.д. Транспорт подключается через log_init() в виде callback-функции.
 * Адаптеры под конкретные транспорты живут в port/log_<transport>/.
 *
 * Использование:
 * @code
 *   // main.c — зарегистрировать транспорт (см. port/log/)
 *   log_init(uart_log_write, NULL);
 *
 *   // любой .c файл
 *   #include "log.h"
 *   LOG_I("BOOT", "Started, tick=%lu", (unsigned long)bsp_tick_get_ms());
 *   LOG_W("SDIO", "Card not detected");
 *   LOG_D("UART", "RX=%u bytes", bsp_uart_host_rx_available());
 * @endcode
 *
 * Формат вывода:
 *   [      1234][I][BOOT] Started, tick=1234\r\n
 *
 * Уровни (LOG_LEVEL задаётся через CMake -DLOG_LEVEL=N):
 *   0 — off      все LOG_* → ((void)0), нулевой ROM
 *   1 — error
 *   2 — warn
 *   3 — info
 *   4 — debug
 *   5 — verbose
 *
 * @note Вызов LOG_* из ISR запрещён (TX-блокирующий, мьютекс).
 */

#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* -------------------------------------------------------------------------- */
    /* Уровни                                                                       */
    /* -------------------------------------------------------------------------- */

#define LOG_LEVEL_OFF     0
#define LOG_LEVEL_ERROR   1
#define LOG_LEVEL_WARN    2
#define LOG_LEVEL_INFO    3
#define LOG_LEVEL_DEBUG   4
#define LOG_LEVEL_VERBOSE 5

/* Если LOG_LEVEL не задан через CMake — максимальный в Debug, off в Release */
#ifndef LOG_LEVEL
#ifdef NDEBUG
#define LOG_LEVEL LOG_LEVEL_OFF
#else
#define LOG_LEVEL LOG_LEVEL_VERBOSE
#endif
#endif

    /* -------------------------------------------------------------------------- */
    /* Callback-тип транспорта                                                      */
    /* -------------------------------------------------------------------------- */

    /**
 * @brief Тип callback-функции транспорта.
 *
 * Вызывается логгером для каждой готовой строки лога.
 * Реализуется в адаптере транспорта (port/log_uart/, port/log_flash/ и т.д.).
 *
 * @param p_buf  Указатель на строку лога (не нуль-терминирована).
 * @param len    Длина строки в байтах.
 * @param p_ctx  Пользовательский контекст, переданный в log_init().
 */
    typedef void (*log_write_cb_t)(const char *p_buf, size_t len, void *p_ctx);

    /* -------------------------------------------------------------------------- */
    /* Инициализация                                                                */
    /* -------------------------------------------------------------------------- */

    /**
 * @brief Инициализировать логгер и зарегистрировать транспорт.
 *
 * Вызывать один раз из main() после инициализации транспорта
 * (например, bsp_uart_host_init()) и мьютекса (log_mutex_init()).
 *
 * @param p_write_cb  Callback транспортного адаптера. NULL — логгер молчит.
 * @param p_ctx     Контекст, передаваемый в write_cb при каждом вызове.
 *                  Для UART обычно NULL.
 */
    void log_init(log_write_cb_t p_write_cb, void *p_ctx);

    /* -------------------------------------------------------------------------- */
    /* Рантайм-уровень (поверх компайл-тайм LOG_LEVEL)                              */
    /* -------------------------------------------------------------------------- */

    /**
 * @brief Задать рантайм-уровень — гейт ПОВЕРХ компайл-тайм LOG_LEVEL (§3.9).
 *
 * ДВА РАЗНЫХ ГЕЙТА, не путать:
 *   - компайл-тайм `LOG_LEVEL` решается при сборке: макросы ниже уровня
 *     физически вырезаны в `((void)0)`, их нет в образе, и гейтить там нечего;
 *   - этот, рантайм-уровень, гейтит то, что ОСТАЛОСЬ скомпилированным, и
 *     меняется в поле без пересборки (пункт меню «Логи»).
 *
 * Следствие: рантайм-уровень не может быть ВЫШЕ компайл-тайм. Продакшн-сборка
 * обязана собираться с `LOG_LEVEL >= LOG_LEVEL_DEBUG`, иначе позиция
 * «Отладка» в меню гейтит пустоту (см. firmware/tft_app/CMakeLists.txt).
 *
 * @param level  LOG_LEVEL_OFF..LOG_LEVEL_VERBOSE; вне диапазона — клампится.
 *               Сообщения строго выше уровня отбрасываются раньше мьютекса и
 *               форматирования, т.е. выключенный лог почти ничего не стоит.
 */
    void log_set_level(int level);

    /**
 * @brief Текущий рантайм-уровень.
 *
 * По умолчанию (до первого вызова log_set_level()) — `LOG_LEVEL_VERBOSE`,
 * т.е. рантайм-гейт НИЧЕГО не режет сверх компайл-тайм. Так ранние сообщения
 * bringup (до загрузки настроек, откуда приходит реальное значение) не
 * теряются молча.
 */
    int log_get_level(void);

    /* -------------------------------------------------------------------------- */
    /* Мьютекс (weak-хуки)                                                         */
    /* -------------------------------------------------------------------------- */

    /**
 * @brief Вернуть текущее время в миллисекундах.
 *
 * Weak-хук: реализация по умолчанию возвращает 0.
 * Переопределяется в адаптере транспорта или в BSP-инициализации:
 *
 * @code
 *   // port/log_uart/log_uart.c или firmware/xxx/src/log_time.c
 *   uint32_t log_get_timestamp_ms(void) { return bsp_tick_get_ms(); }
 * @endcode
 *
 * @return Текущее время в мс.
 */
    uint32_t log_get_timestamp_ms(void);

    /**
 * @brief Инициализировать мьютекс логгера.
 *
 * Bare-metal: weak NOP — не вызывать обязательно.
 * FreeRTOS:   strong-переопределение в firmware/tft_app/src/log_mutex.c.
 *             Вызывать ДО log_init().
 */
    void log_mutex_init(void);

    /** @brief Захватить мьютекс. Weak NOP для bare-metal. */
    void log_mutex_lock(void);

    /** @brief Освободить мьютекс. Weak NOP для bare-metal. */
    void log_mutex_unlock(void);

    /* -------------------------------------------------------------------------- */
    /* Внутренняя функция — не вызывать напрямую                                   */
    /* -------------------------------------------------------------------------- */

    /** @cond INTERNAL */
    void log_write(int level, const char *p_tag, const char *p_fmt, ...)
        __attribute__((format(printf, 3, 4)));
    /** @endcond */

    /* -------------------------------------------------------------------------- */
    /* Публичные макросы                                                            */
    /* -------------------------------------------------------------------------- */

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_E(tag, fmt, ...) log_write(LOG_LEVEL_ERROR, (tag), (fmt), ##__VA_ARGS__)
#else
#define LOG_E(tag, fmt, ...) ((void) 0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_W(tag, fmt, ...) log_write(LOG_LEVEL_WARN, (tag), (fmt), ##__VA_ARGS__)
#else
#define LOG_W(tag, fmt, ...) ((void) 0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_I(tag, fmt, ...) log_write(LOG_LEVEL_INFO, (tag), (fmt), ##__VA_ARGS__)
#else
#define LOG_I(tag, fmt, ...) ((void) 0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_D(tag, fmt, ...) log_write(LOG_LEVEL_DEBUG, (tag), (fmt), ##__VA_ARGS__)
#else
#define LOG_D(tag, fmt, ...) ((void) 0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
#define LOG_V(tag, fmt, ...) log_write(LOG_LEVEL_VERBOSE, (tag), (fmt), ##__VA_ARGS__)
#else
#define LOG_V(tag, fmt, ...) ((void) 0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */