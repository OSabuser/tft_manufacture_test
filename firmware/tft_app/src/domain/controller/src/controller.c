#include "domain/controller.h"

#include "domain/mode_priority.h"

#include <string.h>

void controller_init(controller_ctx_t *p_ctx)
{
    p_ctx->cache = sul_default_state();
}

indication_task_t controller_process(controller_ctx_t *p_ctx, const sul_result_t *p_result)
{
    const sul_mode_t PREV_MODE = sul_resolve_mode(&p_ctx->cache);
    const sul_mode_t NEW_MODE  = sul_resolve_mode(p_result);

    const indication_task_t TASK = {
        .pos_pending       = (strcmp(p_ctx->cache.pos, p_result->pos) != 0),
        .next_pending      = (strcmp(p_ctx->cache.next, p_result->next) != 0),
        .direction_pending = (p_ctx->cache.direction != p_result->direction),
        .mode_pending      = (PREV_MODE != NEW_MODE),
        /* Событийные уровни — presentation/audio реагируют на фронт false→true. */
        .arrival_pending  = (!p_ctx->cache.arrival && p_result->arrival),
        .movement_pending = (!p_ctx->cache.movement && p_result->movement),
        .mode             = NEW_MODE,
    };

    p_ctx->cache = *p_result;

    return TASK;
}
