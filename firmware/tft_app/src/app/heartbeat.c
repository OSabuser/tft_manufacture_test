#include "heartbeat.h"

#include <stddef.h>

/* ── Пороги: ИЕРАРХИЯ, а не набор чисел ─────────────────────────────────────
 *
 * Аппаратный watchdog — 10 c, взводит ЗАГРУЗЧИК (WDE write-once, см.
 * firmware/bootloader/src/main.c); приложение только кормит. Пороги ниже —
 * заведомо меньше его, чтобы причина успела попасть в крэш-запись ДО того,
 * как watchdog добьёт плату:
 *
 *   протух heartbeat (1..3 c) → кормление прекращено + запись причины
 *                             → через ≤10 c аппаратный сброс со внятным
 *                               диагнозом на следующем старте.
 *
 * Пороги — не «время цикла», а «время цикла с большим запасом»: задача может
 * законно задержаться под нагрузкой, а ложный сброс хуже позднего. Порядок
 * величины взят от реальной каденции каждой задачи (см. ниже), запас ×10..×20.
 */

/**
 * @brief Описание наблюдаемой задачи — ДАННЫЕ, как k_mode_priority[] в домене.
 *
 * Добавить задачу под наблюдение = дописать строку сюда и элемент в hb_task_t.
 */
typedef struct
{
    const char *p_name;

    /**
     * Максимум между периодическими отметками. `0` — задача EVENT-DRIVEN:
     * простой законен любой длины, периодической отметки не требует (см.
     * heartbeat.h про две семантики). Для неё живость определяется ТОЛЬКО
     * длительностью защищённых операций.
     */
    uint32_t period_ms;

    /** Максимум на ОДНУ защищённую операцию (heartbeat_enter/leave). */
    uint32_t guard_ms;
} hb_desc_t;

static const hb_desc_t K_TASKS[HB_TASK_COUNT] = {
    /* Цикл 100 мс (WDOG_FEED_PERIOD_MS в task_sul_rx.c). Самый низкий
     * приоритет в системе — под нагрузкой задерживается легче прочих,
     * поэтому запас щедрый. Защищённых операций не имеет: самая долгая её
     * часть — bsp_can_receive() с собственным таймаутом 100 мс. */
    [HB_TASK_SUL_RX] = { .p_name = "sul_rx", .period_ms = 2000U, .guard_ms = 2000U },

    /* Цикл 5 мс (MENU_TICK_MS). Защищённая операция — settings_store_save():
     * стирание+запись сектора QSPI, на порядки дольше каденции задачи. */
    [HB_TASK_MENU] = { .p_name = "menu", .period_ms = 1000U, .guard_ms = 3000U },

    /* EVENT-DRIVEN: спит на ulTaskNotifyTake(portMAX_DELAY) сколько угодно —
     * при отсутствии событий это ШТАТНОЕ состояние, а не зависание.
     * Наблюдается только изнутри операций рисования/композита (после фикса
     * кэшируемого XIP полный кадр — единицы-десятки мс, см.
     * docs/PERF_AND_HANG_INVESTIGATION.md). */
    [HB_TASK_RENDER] = { .p_name = "render", .period_ms = 0U, .guard_ms = 2000U },
};

#define BREADCRUMB_IDLE "idle"

typedef struct
{
    uint32_t last_mark_ms;
    uint32_t enter_ms;
    const char *p_what; /**< NULL — вне защищённой операции */
    bool started;       /**< задача хоть раз подала признак жизни */
} hb_state_t;

/* volatile: пишется из наблюдаемых задач, читается из демона программных
 * таймеров (другой контекст, вытесняет их в любой точке). Атомарность здесь
 * не нужна и не достигается — сама постановка задачи допускает промах на одну
 * итерацию: порог на порядок больше каденции отметок. */
static volatile hb_state_t g_s_state[HB_TASK_COUNT];

/* Защёлка приговора — см. heartbeat_should_feed() в .h про то, почему решение
 * обязано быть необратимым. Обстоятельства фиксируются вместе с ней: крошка
 * протухшей задачи к следующему вызову уже сменилась бы на "idle". */
static bool g_s_stalled = false;
static hb_stall_t g_s_stall;

void heartbeat_reset(void)
{
    for (uint32_t i = 0U; i < (uint32_t) HB_TASK_COUNT; i++)
    {
        g_s_state[i].last_mark_ms = 0U;
        g_s_state[i].enter_ms     = 0U;
        g_s_state[i].p_what       = NULL;
        g_s_state[i].started      = false;
    }

    g_s_stalled       = false;
    g_s_stall.task    = HB_TASK_COUNT;
    g_s_stall.age_ms  = 0U;
    g_s_stall.p_where = NULL;
}

void heartbeat_mark(hb_task_t who, uint32_t now_ms)
{
    if (who >= HB_TASK_COUNT)
    {
        return;
    }

    g_s_state[who].last_mark_ms = now_ms;
    g_s_state[who].started      = true;
}

void heartbeat_enter(hb_task_t who, uint32_t now_ms, const char *p_what)
{
    if (who >= HB_TASK_COUNT)
    {
        return;
    }

    g_s_state[who].enter_ms = now_ms;
    g_s_state[who].p_what   = p_what;
    g_s_state[who].started  = true;
}

void heartbeat_leave(hb_task_t who, uint32_t now_ms)
{
    if (who >= HB_TASK_COUNT)
    {
        return;
    }

    /* Выход из операции — это тоже признак прогресса: для периодической
     * задачи он заменяет отметку, которую та не сделала, пока была занята
     * (иначе после длинной защищённой операции она мгновенно оказалась бы
     * протухшей по period_ms — при том, что только что доказала живость). */
    g_s_state[who].p_what       = NULL;
    g_s_state[who].last_mark_ms = now_ms;
}

/**
 * @brief Возраст задачи: сколько мс она НЕ подавала признаков жизни.
 *
 * `0` — свежа. Разность беззнаковая: корректна при перевороте счётчика
 * времени (тот же приём, что в task_sul_rx.c).
 */
static uint32_t task_age_ms(const volatile hb_state_t *p_state, const hb_desc_t *p_desc,
                            uint32_t now_ms)
{
    if (!p_state->started)
    {
        return 0U; /* ещё не запускалась (bring-up) — свежести не требует */
    }

    if (p_state->p_what != NULL)
    {
        /* Внутри защищённой операции: судим ТОЛЬКО по её длительности —
         * period_ms здесь неприменим (задача занята делом, а не залипла). */
        const uint32_t BUSY_MS = now_ms - p_state->enter_ms;
        return (BUSY_MS >= p_desc->guard_ms) ? BUSY_MS : 0U;
    }

    if (p_desc->period_ms == 0U)
    {
        return 0U; /* event-driven и не занята — простой законен */
    }

    const uint32_t IDLE_MS = now_ms - p_state->last_mark_ms;
    return (IDLE_MS >= p_desc->period_ms) ? IDLE_MS : 0U;
}

bool heartbeat_all_fresh(uint32_t now_ms, hb_task_t *p_stale, uint32_t *p_age_ms)
{
    for (uint32_t i = 0U; i < (uint32_t) HB_TASK_COUNT; i++)
    {
        const uint32_t AGE_MS = task_age_ms(&g_s_state[i], &K_TASKS[i], now_ms);
        if (AGE_MS != 0U)
        {
            if (p_stale != NULL)
            {
                *p_stale = (hb_task_t) i;
            }
            if (p_age_ms != NULL)
            {
                *p_age_ms = AGE_MS;
            }
            return false;
        }
    }

    return true;
}

bool heartbeat_should_feed(uint32_t now_ms, hb_stall_t *p_stall)
{
    if (!g_s_stalled)
    {
        hb_task_t stale = HB_TASK_COUNT;
        uint32_t age_ms = 0U;

        if (heartbeat_all_fresh(now_ms, &stale, &age_ms))
        {
            return true;
        }

        /* Приговор выносится ОДИН раз, вместе с обстоятельствами. */
        g_s_stalled       = true;
        g_s_stall.task    = stale;
        g_s_stall.age_ms  = age_ms;
        g_s_stall.p_where = heartbeat_breadcrumb(stale);
    }

    if (p_stall != NULL)
    {
        *p_stall = g_s_stall;
    }

    return false;
}

const char *heartbeat_task_name(hb_task_t who)
{
    return (who < HB_TASK_COUNT) ? K_TASKS[who].p_name : "?";
}

const char *heartbeat_breadcrumb(hb_task_t who)
{
    if (who >= HB_TASK_COUNT)
    {
        return "?";
    }

    const char *p_what = g_s_state[who].p_what;
    return (p_what != NULL) ? p_what : BREADCRUMB_IDLE;
}
