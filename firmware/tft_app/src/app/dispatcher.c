/**
 * @file  dispatcher.c
 * @brief Диспетчерский вход (opto IN1/IN2, §3.4) — «Вызов подан» / «Вызов
 *        принят». НЕ задача (нет своего while(true)) — тонкая обвязка
 *        bsp_opto поверх уже готового BSP-драйвера (bsp/opto): каждый тик
 *        софт-таймера (dispatcher_poll(), см. input_poll_cb в task_menu.c)
 *        заново читает оба канала, пишет g_dispatcher_indication и будит
 *        render_task при изменении.
 *
 * ARCH §8 п.3: опто — «локальный вход», не данные СУЛ; вливается на уровне
 * презентации (ARCH §4, dataflow — opto отдельным входом прямо в
 * ui.render()), не через controller. Приоритет — высший из всех режимов
 * индикации (согласовано с пользователем), безусловно перекрывает и обычную
 * позицию, и любой режим СУЛ; работает независимо от связи со станцией.
 *
 * НЕПРЕРЫВНЫЙ ОПРОС, БЕЗ колбэков bsp_opto (callbacks = {NULL,...}) — как
 * в OLD_PROJECT_TFT8_UKL (`tft_refresh_task`): там `bsp_opto_read()`
 * читается каждую итерацию задачи, а фронт детектируется самим потребителем
 * через пару state[0]/state[1], колбэк вообще не регистрируется. Первая
 * версия этого файла была реактивной: пересчитывала состояние ТОЛЬКО изнутри
 * колбэка `bsp_opto_process()`, вызываемого на подтверждённую смену канала.
 * На стенде это дало баг (см. PLAN.md §3.4): «ВЫЗОВ» не сбрасывался при
 * снятии физического сигнала — залипал до случайного следующего фронта на
 * ЛЮБОМ канале, который наконец пересчитывал состояние с нуля. Причина —
 * не гонка в bsp_opto (там её и не было, судя по истории OLD_PROJECT), а
 * отсутствие самовосстановления: чисто реактивная схема имеет ровно один
 * шанс заметить каждое изменение, и если он почему-то пропадает — состояние
 * замирает НАВСЕГДА. Непрерывный опрос вместо этого переспрашивает
 * `bsp_opto_read()` заново каждые 5 мс — единичный сбой (где бы он ни
 * случился) чинится на следующем же тике, тот же принцип, что и у
 * bsp_button (тоже без колбэков, тоже опрашивается каждый тик).
 *
 * RS_RX (bsp_opto третий канал, бинарный протокол) здесь не используется —
 * остаётся под LPUART3 (rs_as_gpio=false), это для другого будущего сценария.
 */

#include "app_tasks.h"

#include "bsp/opto.h"
#include "log/log.h"

#define LOG_TAG "dispatcher"

/**
 * @brief Вычислить индикацию из ТЕКУЩЕГО состояния обоих каналов. ОТВЕТ
 *        (IN2) перебивает ВЫЗОВ (IN1), если оба почему-то активны
 *        одновременно (согласовано с пользователем).
 */
static dispatcher_indication_t resolve_indication(void)
{
    const bool ANSWER_ACTIVE = (bsp_opto_read(BSP_OPTO_CH_IN2) == BSP_OPTO_STATE_ACTIVE);
    const bool CALL_ACTIVE   = (bsp_opto_read(BSP_OPTO_CH_IN1) == BSP_OPTO_STATE_ACTIVE);

    return ANSWER_ACTIVE ? DISPATCHER_INDICATION_ANSWER
         : CALL_ACTIVE   ? DISPATCHER_INDICATION_CALL
                          : DISPATCHER_INDICATION_NONE;
}

static const char *indication_name(dispatcher_indication_t v)
{
    switch (v)
    {
        case DISPATCHER_INDICATION_CALL:   return "CALL";
        case DISPATCHER_INDICATION_ANSWER: return "ANSWER";
        case DISPATCHER_INDICATION_NONE:
        default:                           return "NONE";
    }
}

/**
 * @brief Залогировать переход строго по фронтам (не по значению каждый
 *        тик — dispatcher_poll() зовёт это ТОЛЬКО когда индикация реально
 *        изменилась). ВЫКЛ->ВКЛ и ВКЛ->ВЫКЛ — раздельные строки, каждая по
 *        своему "режиму" (CALL/ANSWER); прямой переход CALL<->ANSWER (оба —
 *        не NONE) — это одновременно выключение старого И появление нового,
 *        печатаются обе строки.
 */
static void log_transition(dispatcher_indication_t old_state, dispatcher_indication_t new_state)
{
    if (old_state != DISPATCHER_INDICATION_NONE)
    {
        LOG_I(LOG_TAG, "mode %s disabled", indication_name(old_state));
    }
    if (new_state != DISPATCHER_INDICATION_NONE)
    {
        LOG_I(LOG_TAG, "mode %s appeared", indication_name(new_state));
    }
}

static const bsp_opto_config_t K_OPTO_CONFIG = {
    .callbacks   = { NULL, NULL, NULL }, /* непрерывный опрос — см. докстрок файла */
    .modes       = { BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL },
    .edges       = { BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING },
    .rs_as_gpio  = false, /* RS_RX остаётся под LPUART3 — не наш случай (см. докстрок файла) */
    .debounce_ms = 10U,   /* середина рекомендованного диапазона 5-10 мс, bsp/opto/opto.h */
};

void dispatcher_init(void)
{
    (void) bsp_opto_init(&K_OPTO_CONFIG);

    /* Захват состояния СРАЗУ — до первого dispatcher_poll() (следующий тик
     * софт-таймера) экран не должен показывать NONE, если вызов уже висит на
     * момент старта устройства (станция держит сигнал постоянно, не
     * импульсом). */
    g_dispatcher_indication = resolve_indication();
}

void dispatcher_poll(void)
{
    const dispatcher_indication_t NEW_STATE = resolve_indication();

    if (NEW_STATE != g_dispatcher_indication)
    {
        log_transition(g_dispatcher_indication, NEW_STATE);

        g_dispatcher_indication = NEW_STATE;

        if (g_render_task_handle != NULL)
        {
            (void) xTaskNotifyGive(g_render_task_handle);
        }
    }
}
