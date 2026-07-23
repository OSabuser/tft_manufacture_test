#include "domain/sul/demo.h"

#include "demo_route.h"

#include <stdio.h>

#define DEMO_SPEED_MAX 2U

/* Тиков (вызовов decode()) на один шаг маршрута, по индексу скорости
 * (0=медленно/1=норма/2=быстро). Тик = один опрос sul_rx_task (~100 мс,
 * см. task_sul_rx.c) -> ~2с/~1с/~0.4с на шаг, полный круг ~50/25/10с. */
static const uint16_t K_TICKS_PER_STEP[DEMO_SPEED_MAX + 1U] = { 20U, 10U, 4U };

/* Спецрежим шага -> соответствующий булев сигнал sul_result_t (§6). Ровно
 * один активен за раз (SUL_MODE_NORMAL -> все false) — этого достаточно для
 * скриптованной демонстрации; настоящая одновременность нескольких сигналов
 * (как теоретически возможно у живого НКУ-CAN, PACKET2+PACKET4 независимо)
 * демо не воспроизводит — не тот масштаб задачи, при необходимости
 * расширяется в demo_route.h (см. demo_step_t). */
static void apply_mode(sul_mode_t mode, sul_result_t *p_state)
{
    p_state->overload    = (mode == SUL_MODE_OVERLOAD);
    p_state->fire_alarm  = (mode == SUL_MODE_FIRE_ALARM);
    p_state->lading      = (mode == SUL_MODE_LADING);
    p_state->maintenance = (mode == SUL_MODE_MAINTENANCE);
    p_state->fireman     = (mode == SUL_MODE_FIREMAN);
    p_state->seismic     = (mode == SUL_MODE_SEISMIC);
}

static void apply_step(demo_ctx_t *p_ctx)
{
    const demo_step_t *p_step = &K_DEMO_ROUTE[p_ctx->step];

    (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s", p_step->p_pos);
    p_ctx->state.next[0]   = '\0'; /* демо не показывает "следующий этаж" */
    p_ctx->state.direction = p_step->direction;
    p_ctx->state.movement  = (p_step->direction != SUL_DIR_NONE);
    p_ctx->state.arrival   = p_step->arrival;
    p_ctx->state.floor_num = p_step->floor_num;
    apply_mode(p_step->mode, &p_ctx->state);
}

void demo_init(demo_ctx_t *p_ctx)
{
    p_ctx->state            = sul_default_state();
    p_ctx->step             = 0U;
    p_ctx->speed_idx        = 1U; /* "Норма" по умолчанию */
    p_ctx->ticks_since_step = 0U;
    apply_step(p_ctx);
}

void demo_set_speed(demo_ctx_t *p_ctx, uint8_t speed_idx)
{
    p_ctx->speed_idx = (speed_idx <= DEMO_SPEED_MAX) ? speed_idx : DEMO_SPEED_MAX;
}

sul_status_t demo_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out)
{
    (void) p_frame; /* демо не читает содержимое кадра, только факт вызова (тик) */
    demo_ctx_t *p_state = (demo_ctx_t *) p_ctx;

    p_state->ticks_since_step++;
    if (p_state->ticks_since_step >= K_TICKS_PER_STEP[p_state->speed_idx])
    {
        p_state->ticks_since_step = 0U;
        p_state->step             = (uint8_t) ((p_state->step + 1U) % DEMO_ROUTE_LEN);
        apply_step(p_state);
    }

    *p_out = p_state->state;
    return SUL_STATUS_OK;
}
