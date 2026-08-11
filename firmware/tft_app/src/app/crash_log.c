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

/** Имя задачи: FreeRTOS-имена и наши («sul_rx»/«menu»/«render») коротки. */
#define CRASH_NAME_MAX 16U

/* Крошка — ОТДЕЛЬНЫЙ, больший лимит. На HW-проверке §3.7 общий лимит 16 съел
 * последний символ: в лог ушло 'render:menu-ope' вместо 'render:menu-open'.
 * Крошки соседних веток одной задачи различаются В КОНЦЕ строки
 * («render:menu-open» / «render:menu-nav» / «render:indication»), т.е. обрезка
 * бьёт ровно по различающей части — при том, что крошка и существует, чтобы
 * различать. 32 — с запасом к самой длинной сейчас (18: «render:first-frame»,
 * «menu:settings-save»). */
#define CRASH_WHERE_MAX 32U

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

void crash_log_report_previous(void)
{
    /* Аппаратный свидетель, доступный ПРИЛОЖЕНИЮ, ровно один — WDOG1->WRSR
     * (bsp_wdog_reset_was_timeout()): регистр read-only и самоочищается на
     * каждый сброс, поэтому потребления не требует и работает на любой стадии
     * цепочки загрузки.
     *
     * Полного источника сброса (питание / кнопка / отладчик / перегрев) у
     * приложения НЕТ и быть не может: SRC->SRSR — ресурс однократного
     * потребления, и его забирает ЗАГРУЗЧИК (bsp_boot_state_init() читает бит
     * POR для счётчика попыток recovery и очищает регистр целиком) задолго до
     * нашего main(). Подтверждено на стенде 2026-08-11: SRSR=0 даже после
     * снятия питания. Чтобы источник дошёл сюда, загрузчик должен передавать
     * его явно — см. PLAN.md §3.8 и bsp/reset/README.md. */
    const bool WDOG_TIMEOUT = bsp_wdog_reset_was_timeout();

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

        char where[CRASH_WHERE_MAX];
        for (uint32_t i = 0U; i < CRASH_WHERE_MAX; i++)
        {
            where[i] = g_s_crash.where[i];
        }
        where[CRASH_WHERE_MAX - 1U] = '\0';

        g_s_crash.magic = 0U; /* один раз — следующий старт будет чистым */

        /* WDOG1.TOUT рядом с причиной различает ДВА пути сброса, которые сама
         * причина не различает: залипание задачи добивает watchdog (TOUT=1),
         * а отказ в коде сбрасывается нами через NVIC_SystemReset() (TOUT=0). */
        if (CAUSE == (uint32_t) CRASH_CAUSE_TASK_STALL)
        {
            /* Для протухшей задачи поля переиспользованы: name — какая именно,
             * where — где она залипла, pc — сколько мс молчала. */
            LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: %s task='%s' залипла в '%s', молчала %u мс [TOUT=%u]",
                  cause_name(CAUSE), name, where, (unsigned) PC, WDOG_TIMEOUT ? 1U : 0U);
        }
        else
        {
            LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: %s task='%s' pc=0x%08X lr=0x%08X [TOUT=%u]",
                  cause_name(CAUSE), name, (unsigned) PC, (unsigned) LR, WDOG_TIMEOUT ? 1U : 0U);
        }
        return;
    }

    /* Записи нет, а watchdog всё же сработал. Супервизор (§3.7) причину пишет
     * ЗАРАНЕЕ, поэтому её отсутствие здесь означает отказ САМОГО кормильца —
     * демона программных таймеров — либо срыв ещё до его старта. */
    if (WDOG_TIMEOUT)
    {
        LOG_E(LOG_TAG, "ПРЕДЫДУЩИЙ СБРОС: WATCHDOG, крэш-записи НЕТ — "
                       "не отработал сам супервизор/демон таймеров");
        return;
    }

    /* Ни записи, ни таймаута. Что именно — питание, кнопка или отладчик —
     * приложению неизвестно (см. про SRSR выше), поэтому перечисляем честно,
     * а не выдаём догадку за факт. */
    LOG_I(LOG_TAG, "предыдущий сброс: штатный (питание/кнопка/отладчик)");
}
