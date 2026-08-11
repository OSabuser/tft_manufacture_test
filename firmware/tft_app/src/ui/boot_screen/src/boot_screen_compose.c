/*
 * Загрузочный экран — сборка строк из данных. ЧИСТАЯ часть (§3.10).
 *
 * Отдельная единица трансляции от рендера намеренно: здесь нет ни gfx, ни bsp,
 * поэтому «какие строки появляются и как форматируются» проверяется на хосте
 * (tests/host/tft_app_boot_screen), а не на стенде.
 */

#include "ui/boot_screen.h"

#include <stdarg.h>
#include <stdio.h>

/** Добавить строку, если ещё есть место. */
static void add(boot_screen_lines_t *p_out, const char *p_fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void add(boot_screen_lines_t *p_out, const char *p_fmt, ...)
{
    if (p_out->count >= BOOT_SCREEN_MAX_LINES)
    {
        return; /* переполнение молча отбрасывает хвост — экран конечен */
    }

    va_list args;
    va_start(args, p_fmt);
    (void) vsnprintf(p_out->line[p_out->count], BOOT_SCREEN_LINE_LEN, p_fmt, args);
    va_end(args);

    p_out->count++;
}

void boot_screen_compose(const boot_screen_info_t *p_info, boot_screen_lines_t *p_out)
{
    p_out->count      = 0U;
    p_out->alert_line = BOOT_SCREEN_NO_ALERT;

    /* ── Версии ──────────────────────────────────────────────────────────
     * «н/д» вместо выдуманного значения — принципиально: экран, который
     * уверенно показывает неверную версию, хуже экрана, который признаётся,
     * что не знает. */
    if (p_info->app_version_valid)
    {
        add(p_out, "Версия: %u.%u.%u", (unsigned) p_info->app_major, (unsigned) p_info->app_minor,
            (unsigned) p_info->app_revision);
    }
    else
    {
        add(p_out, "Версия: н/д");
    }

    add(p_out, "Загрузчик: %s",
        (p_info->p_bootloader_version != NULL) ? p_info->p_bootloader_version : "н/д");

    /* ── Железо и протокол ───────────────────────────────────────────────── */
    if (p_info->p_panel != NULL)
    {
        add(p_out, "Панель: %s", p_info->p_panel);
    }

    if (p_info->p_protocol != NULL)
    {
        /* Параметр — в той же строке: «НКУ-CAN, Адрес 1». Именно адрес чаще
         * всего и оказывается причиной «плата живая, экран пустой», поэтому
         * он рядом с протоколом, а не отдельной строкой ниже. */
        if (p_info->p_param_label != NULL)
        {
            add(p_out, "Протокол: %s, %s %u", p_info->p_protocol, p_info->p_param_label,
                (unsigned) p_info->param_value);
        }
        else
        {
            add(p_out, "Протокол: %s", p_info->p_protocol);
        }
    }

    /* ── UID чипа ────────────────────────────────────────────────────────
     * Двумя группами по 4 байта — чтобы диктовать по телефону. */
    if (p_info->uid_valid)
    {
        add(p_out, "ID: %02X%02X%02X%02X %02X%02X%02X%02X", p_info->uid[0], p_info->uid[1],
            p_info->uid[2], p_info->uid[3], p_info->uid[4], p_info->uid[5], p_info->uid[6],
            p_info->uid[7]);
    }

    /* ── Причина сброса — ТОЛЬКО когда она есть ──────────────────────────
     * Штатный старт строки не даёт вовсе: на каждом включении она была бы
     * шумом, а так само её присутствие — сигнал. */
    if (p_info->p_reset_cause != NULL)
    {
        p_out->alert_line = p_out->count; /* до add() — она и станет этой строкой */
        add(p_out, "Сброс: %s", p_info->p_reset_cause);

        /* Место — ОТДЕЛЬНОЙ строкой, а не в скобках рядом с причиной: вместе
         * получалось до 38 символов, а в окно шрифтом JBMono24 влезает 33 —
         * обрезало бы ровно хвост крошки, то есть различающую часть (та же
         * ошибка, что уже ловили на CRASH_WHERE_MAX в §3.7). Цветом выделена
         * только строка причины: она ловит взгляд, место — уточнение под ней. */
        if (p_info->p_reset_where != NULL)
        {
            add(p_out, "Место: %s", p_info->p_reset_where);
        }
    }

    /* ── Слоты будущих фаз ───────────────────────────────────────────────
     * NULL — строки нет. Фазам 4/5 останется подставить значение. */
    if (p_info->p_style_name != NULL)
    {
        add(p_out, "Стиль: %s", p_info->p_style_name);
    }

    if (p_info->p_custom != NULL)
    {
        add(p_out, "%s", p_info->p_custom); /* без подписи — это строка клиента */
    }
}
