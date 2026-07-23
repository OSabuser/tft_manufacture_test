/**
 * @file  log.c
 * @brief Реализация платформонезависимого логгера.
 *
 * Единственная внешняя зависимость — стандартная библиотека C
 * (vsnprintf, stdarg). Транспорт абстрагирован через callback.
 *
 * Компилируется всегда. При LOG_LEVEL=0 макросы в log.h
 * разворачиваются в ((void)0) и log_write() не вызывается вообще —
 * но сам .c файл в ROM не попадает: линкер выкидывает неиспользуемые
 * секции (--gc-sections).
 */

#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* -------------------------------------------------------------------------- */
/* Конфигурация                                                                 */
/* -------------------------------------------------------------------------- */

/** Размер статического буфера одной строки (включая префикс, \r\n, \0). */
#ifndef LOG_BUF_SIZE
#define LOG_BUF_SIZE 256U
#endif

/** Перевод строки. \r\n для MCU-Link VCOM. */
#ifndef LOG_NEWLINE
#define LOG_NEWLINE "\r\n"
#endif

/* -------------------------------------------------------------------------- */
/* Внутреннее состояние                                                         */
/* -------------------------------------------------------------------------- */

static log_write_cb_t g_s_write_cb = NULL;
static void *g_s_p_ctx             = NULL;

/* true по умолчанию — до первого log_set_enabled() (обычно из настроек,
 * §3.6) ранние сообщения bringup не должны теряться молча. */
static bool g_s_log_enabled = true;

/* -------------------------------------------------------------------------- */
/* Weak-хуки мьютекса — NOP для bare-metal                                    */
/* -------------------------------------------------------------------------- */

__attribute__((weak)) void log_mutex_init(void)
{
}
__attribute__((weak)) void log_mutex_lock(void)
{
}
__attribute__((weak)) void log_mutex_unlock(void)
{
}
__attribute__((weak)) uint32_t log_get_timestamp_ms(void)
{
    return 0U;
}

/* -------------------------------------------------------------------------- */
/* Вспомогательное: метка уровня                                               */
/* -------------------------------------------------------------------------- */

static char level_char(int level)
{
    switch (level)
    {
    case LOG_LEVEL_ERROR:
        return 'E';
    case LOG_LEVEL_WARN:
        return 'W';
    case LOG_LEVEL_INFO:
        return 'I';
    case LOG_LEVEL_DEBUG:
        return 'D';
    case LOG_LEVEL_VERBOSE:
        return 'V';
    default:
        return '?';
    }
}

/* -------------------------------------------------------------------------- */
/* Публичный API                                                                */
/* -------------------------------------------------------------------------- */

void log_init(log_write_cb_t p_write_cb, void *p_ctx)
{
    g_s_write_cb = p_write_cb;
    g_s_p_ctx    = p_ctx;
}

void log_set_enabled(bool enabled)
{
    g_s_log_enabled = enabled;
}

bool log_is_enabled(void)
{
    return g_s_log_enabled;
}

void log_write(int level, const char *p_tag, const char *p_fmt, // NOLINT(readability-function-size)
               ...)
{
    if (!g_s_log_enabled || (g_s_write_cb == NULL))
    {
        return;
    }

    /* Статический буфер: под мьютексом → нет гонки в FreeRTOS,
     * в bare-metal гонки нет по определению.                    */
    static char s_buf[LOG_BUF_SIZE];

    log_mutex_lock();

    /* Префикс: [timestamp][L][TAG] */
    int prefix_len = snprintf(s_buf, sizeof(s_buf), "[%10lu][%c][%s] ",
                              (unsigned long) log_get_timestamp_ms(), level_char(level), p_tag);

    if (prefix_len < 0)
    {
        log_mutex_unlock();
        return;
    }

    /* Сообщение пользователя */
    size_t prefix_sz = (size_t) prefix_len;
    size_t remaining = (prefix_sz < sizeof(s_buf)) ? (sizeof(s_buf) - prefix_sz) : 0U;

    if (remaining > 0U)
    {
        va_list args;
        va_start(args, p_fmt);
        int msg_len = vsnprintf(s_buf + prefix_sz, remaining, p_fmt, args);
        va_end(args);

        if (msg_len > 0)
        {
            prefix_sz += (size_t) msg_len;
        }
    }

    /* ВАЖНО: vsnprintf возвращает число символов которые *хотел* записать,
     * а не сколько реально влезло. Если сообщение длиннее remaining,
     * prefix_sz > sizeof(s_buf). Без clamp ниже вычисление avail даёт
     * size_t underflow → огромное число → выход за границу буфера (UB). */
    if (prefix_sz > sizeof(s_buf))
    {
        prefix_sz = sizeof(s_buf);
    }

    /* Перевод строки — дописать если влезает */
    const char *p_nl = LOG_NEWLINE;
    size_t nl_len    = sizeof(LOG_NEWLINE) - 1U; /* без \0 */
    size_t avail     = sizeof(s_buf) - prefix_sz;

    if (avail > nl_len)
    {
        for (size_t i = 0U; i < nl_len; i++)
        {
            s_buf[prefix_sz + i] = p_nl[i];
        }
        prefix_sz += nl_len;
    }
    else
    {
        /* Буфер переполнен — принудительно завершить строку */
        size_t tail = sizeof(s_buf) - nl_len;
        for (size_t i = 0U; i < nl_len; i++)
        {
            s_buf[tail + i] = p_nl[i];
        }
        prefix_sz = sizeof(s_buf);
    }

    g_s_write_cb(s_buf, prefix_sz, g_s_p_ctx);

    log_mutex_unlock();
}