#include "crash_log.h"

#include "FreeRTOS.h"
#include "MIMXRT1052.h"
#include "bsp/wdog.h"
#include "log/log.h"
#include "task.h"

#include <stdbool.h>

#define LOG_TAG "crash"

/* 'CRA3' — версия раскладки записи. Бампается на КАЖДОЕ изменение раскладки:
 * запись из образа с прежней раскладкой, оставшаяся в .noinit после обновления
 * прошивки БЕЗ снятия питания, иначе прошла бы проверку магии и была бы
 * разобрана по новым полям. История: 'CRAH' → 'CRA2' (появилось `where`) →
 * 'CRA3' (крошка расширена, см. ниже). */
#define CRASH_MAGIC 0x43524133U

/**
 * Крэш-запись. `.noinit` — не обнуляется startup'ом, переживает сброс ядра
 * (см. crash_log.h). volatile: пишется перед сбросом, читается после — для
 * компилятора это «внешнее» изменение.
 */
typedef struct
{
    uint32_t magic;
    uint32_t cause;
    uint32_t pc;                 /**< HardFault: PC. TASK_STALL: возраст heartbeat, мс */
    uint32_t lr;                 /**< HardFault: LR. Иначе 0                           */
    char name[CRASH_NAME_MAX];   /**< имя задачи                       */
    char where[CRASH_WHERE_MAX]; /**< крошка (TASK_STALL); иначе пусто */
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
    case CRASH_CAUSE_TASK_STALL:
        return "TASK STALL";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Копирование строки с усечением в поле записи.
 *
 * Ручное, без strncpy: тянуть <string.h> во враждебный контекст (возможно,
 * ISR с повреждённым стеком) незачем, а имя задачи могло прийти из битого TCB
 * — читаем не больше буфера.
 */
static void copy_truncated(volatile char *p_dst, const char *p_src, uint32_t cap)
{
    uint32_t i = 0U;
    if (p_src != NULL)
    {
        for (; (i < (cap - 1U)) && (p_src[i] != '\0'); i++)
        {
            p_dst[i] = p_src[i];
        }
    }
    p_dst[i] = '\0';
}

/**
 * @brief Заполнить запись. Магия — ПОСЛЕДНЕЙ: запись считается валидной только
 *        когда всё остальное уже лежит (сброс посреди заполнения не даст
 *        полуверную запись).
 */
static void record(crash_cause_t cause, const char *p_name, const char *p_where, uint32_t a,
                   uint32_t b)
{
    g_s_crash.cause = (uint32_t) cause;
    g_s_crash.pc    = a;
    g_s_crash.lr    = b;

    copy_truncated(g_s_crash.name, p_name, CRASH_NAME_MAX);
    copy_truncated(g_s_crash.where, p_where, CRASH_WHERE_MAX);

    g_s_crash.magic = CRASH_MAGIC;
}

void crash_log_record_and_reset(crash_cause_t cause, const char *p_name, uint32_t pc, uint32_t lr)
{
    record(cause, p_name, NULL, pc, lr);

    __DSB();
    NVIC_SystemReset();

    for (;;) /* NVIC_SystemReset() не возвращается; страховка от оптимизатора */
    {
    }
}

void crash_log_record_stall(const char *p_task, const char *p_where, uint32_t age_ms)
{
    /* Только ПЕРВАЯ причина: см. crash_log.h — супервизор зовёт нас каждые
     * 5 мс до самого срабатывания watchdog, и задачи, протухшие следом как
     * лавина от того же залипания, затёрли бы первичный диагноз. */
    if (g_s_crash.magic == CRASH_MAGIC)
    {
        return;
    }

    record(CRASH_CAUSE_TASK_STALL, p_task, p_where, age_ms, 0U);

    /* Сброса ЗДЕСЬ нет и быть не должно: его сделает аппаратный watchdog,
     * которого супервизор перестал кормить (см. crash_log.h, два режима). */
}

void crash_log_take_previous(crash_info_t *p_out)
{
    p_out->wdog_timeout = bsp_wdog_reset_was_timeout();
    p_out->valid        = (g_s_crash.magic == CRASH_MAGIC);

    if (!p_out->valid)
    {
        p_out->cause    = CRASH_CAUSE_NONE;
        p_out->pc       = 0U;
        p_out->lr       = 0U;
        p_out->name[0]  = '\0';
        p_out->where[0] = '\0';
        return;
    }

    /* Поля volatile — читаем по одному разу. */
    p_out->cause = (crash_cause_t) g_s_crash.cause;
    p_out->pc    = g_s_crash.pc;
    p_out->lr    = g_s_crash.lr;

    for (uint32_t i = 0U; i < CRASH_NAME_MAX; i++)
    {
        p_out->name[i] = g_s_crash.name[i];
    }
    p_out->name[CRASH_NAME_MAX - 1U] = '\0';

    for (uint32_t i = 0U; i < CRASH_WHERE_MAX; i++)
    {
        p_out->where[i] = g_s_crash.where[i];
    }
    p_out->where[CRASH_WHERE_MAX - 1U] = '\0';

    /* Гасим — запись однократного потребления (см. crash_log.h). Дальше все
     * потребители (лог, загрузочный экран) читают снятую копию. */
    g_s_crash.magic = 0U;
}

const char *crash_log_cause_name(const crash_info_t *p_info)
{
    return p_info->valid ? cause_name((uint32_t) p_info->cause) : NULL;
}

void crash_log_report(const crash_info_t *p_info)
{
    if (p_info->valid)
    {
        /* WDOG1.TOUT рядом с причиной различает ДВА пути сброса, которые сама
         * причина не различает: залипание задачи добивает watchdog (TOUT=1),
         * а отказ в коде сбрасывается нами через NVIC_SystemReset() (TOUT=0). */
        if (p_info->cause == CRASH_CAUSE_TASK_STALL)
        {
            /* Для протухшей задачи поля переиспользованы: name — какая именно,
             * where — где она залипла, pc — сколько мс молчала. */
            LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: %s task='%s' залипла в '%s', молчала %u мс [TOUT=%u]",
                  cause_name((uint32_t) p_info->cause), p_info->name, p_info->where,
                  (unsigned) p_info->pc, p_info->wdog_timeout ? 1U : 0U);
        }
        else
        {
            LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: %s task='%s' pc=0x%08X lr=0x%08X [TOUT=%u]",
                  cause_name((uint32_t) p_info->cause), p_info->name, (unsigned) p_info->pc,
                  (unsigned) p_info->lr, p_info->wdog_timeout ? 1U : 0U);
        }
        return;
    }

    /* Записи нет, а watchdog всё же сработал. Супервизор (§3.7) причину пишет
     * ЗАРАНЕЕ, поэтому её отсутствие здесь означает отказ САМОГО кормильца —
     * демона программных таймеров — либо срыв ещё до его старта. */
    if (p_info->wdog_timeout)
    {
        LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: WATCHDOG, крэш-записи НЕТ — "
                       "не отработал сам супервизор/демон таймеров");
        return;
    }

    /* Ни записи, ни таймаута. Что именно — питание, кнопка или отладчик —
     * приложению неизвестно (SRC->SRSR потребляет загрузчик, см. §3.8),
     * поэтому перечисляем честно, а не выдаём догадку за факт. */
    LOG_I(LOG_TAG, "предыдущий сброс: штатный (питание/кнопка/отладчик)");
}
