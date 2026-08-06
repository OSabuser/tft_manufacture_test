#include "crash_log.h"

#include "FreeRTOS.h"
#include "MIMXRT1052.h"
#include "bsp/wdog.h"
#include "log/log.h"
#include "task.h"

#include <stdbool.h>

#define LOG_TAG "crash"

#define CRASH_MAGIC    0x43524148U /* 'CRAH' — запись валидна */
#define CRASH_NAME_MAX 16U

/**
 * Крэш-запись. `.noinit` — не обнуляется startup'ом, переживает сброс ядра
 * (см. crash_log.h). volatile: пишется перед сбросом, читается после — для
 * компилятора это «внешнее» изменение.
 */
typedef struct
{
    uint32_t magic;
    uint32_t cause;
    uint32_t pc;
    uint32_t lr;
    char name[CRASH_NAME_MAX];
} crash_record_t;

static volatile crash_record_t g_s_crash __attribute__((section(".noinit")));

static const char *cause_name(uint32_t cause)
{
    switch (cause)
    {
    case CRASH_CAUSE_STACK_OVERFLOW:
        return "STACK OVERFLOW";
    case CRASH_CAUSE_HARDFAULT:
        return "HARDFAULT";
    case CRASH_CAUSE_MALLOC_FAILED:
        return "MALLOC FAILED";
    case CRASH_CAUSE_STARVATION:
        return "STARVATION (кормилец WDOG не получал CPU)";
    default:
        return "UNKNOWN";
    }
}

/* ── Детектор голодания ─────────────────────────────────────────────────────
 *
 * Порог заметно НИЖЕ аппаратного таймаута (10 c, взводит загрузчик) — надо
 * успеть записать причину и сбросить самим, иначе watchdog приходит молча.
 */
#define STARVATION_LIMIT_MS 4000U

static volatile uint32_t g_s_last_feed_tick;
static volatile bool g_s_feed_seen;
static const char *volatile g_s_breadcrumb = "idle";

void crash_log_set_breadcrumb(const char *p_where)
{
    g_s_breadcrumb = p_where;
}

void crash_log_feed_mark(void)
{
    g_s_last_feed_tick = (uint32_t) xTaskGetTickCount();
    g_s_feed_seen      = true;
}

void crash_log_check_starvation(void)
{
    if (!g_s_feed_seen)
    {
        return; /* кормилец ещё не стартовал (bring-up) — не считаем голоданием */
    }

    const uint32_t SINCE_MS =
        ((uint32_t) xTaskGetTickCount() - g_s_last_feed_tick) * portTICK_PERIOD_MS;

    if (SINCE_MS >= STARVATION_LIMIT_MS)
    {
        crash_log_record_and_reset(CRASH_CAUSE_STARVATION, g_s_breadcrumb, SINCE_MS, 0U);
    }
}

void crash_log_record_and_reset(crash_cause_t cause, const char *p_name, uint32_t pc, uint32_t lr)
{
    g_s_crash.cause = (uint32_t) cause;
    g_s_crash.pc    = pc;
    g_s_crash.lr    = lr;

    /* Ручное копирование с усечением: strncpy тянуть в этот контекст незачем,
     * а имя задачи могло прийти из повреждённого TCB — читаем не больше буфера. */
    uint32_t i = 0U;
    if (p_name != NULL)
    {
        for (; (i < (CRASH_NAME_MAX - 1U)) && (p_name[i] != '\0'); i++)
        {
            g_s_crash.name[i] = p_name[i];
        }
    }
    g_s_crash.name[i] = '\0';

    /* Магию — ПОСЛЕДНЕЙ: запись считается валидной только когда всё остальное
     * уже лежит (сброс посреди заполнения не даст полуверную запись). */
    g_s_crash.magic = CRASH_MAGIC;

    __DSB();
    NVIC_SystemReset();

    for (;;) /* NVIC_SystemReset() не возвращается; страховка от оптимизатора */
    {
    }
}

void crash_log_report_previous(void)
{
    if (g_s_crash.magic == CRASH_MAGIC)
    {
        /* Копия до гашения — поля volatile, читаем по одному разу. */
        const uint32_t CAUSE = g_s_crash.cause;
        const uint32_t PC    = g_s_crash.pc;
        const uint32_t LR    = g_s_crash.lr;
        char name[CRASH_NAME_MAX];
        for (uint32_t i = 0U; i < CRASH_NAME_MAX; i++)
        {
            name[i] = g_s_crash.name[i];
        }
        name[CRASH_NAME_MAX - 1U] = '\0';

        g_s_crash.magic = 0U; /* один раз — следующий старт будет чистым */

        if (CAUSE == (uint32_t) CRASH_CAUSE_STARVATION)
        {
            /* Для голодания поля переиспользованы: name — «хлебная крошка»
             * (где залипли), pc — сколько мс кормилец не отмечался. */
            LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: %s залипли в '%s', без кормления %u мс",
                  cause_name(CAUSE), name, (unsigned) PC);
        }
        else
        {
            LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: %s task='%s' pc=0x%08X lr=0x%08X", cause_name(CAUSE),
                  name, (unsigned) PC, (unsigned) LR);
        }
        return;
    }

    /* Крэш-записи нет. Если watchdog всё же сработал — значит задача-кормилец
     * не получала CPU (голодание), а не отказ в коде: это принципиально другой
     * диагноз, см. crash_log.h. */
    if (bsp_wdog_reset_was_timeout())
    {
        LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: WATCHDOG TIMEOUT (крэш-записи нет — "
                       "задача-кормилец не получала CPU)");
        return;
    }

    LOG_I(LOG_TAG, "предыдущий сброс: штатный (POR/внешний)");
}
