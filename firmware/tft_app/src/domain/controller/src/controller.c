#include "domain/controller.h"

#include <string.h>

void controller_init(controller_ctx_t *p_ctx)
{
    p_ctx->cache = sul_default_state();
}

indication_task_t controller_process(controller_ctx_t *p_ctx, const sul_result_t *p_result)
{
    const indication_task_t task = {
        .pos_pending       = (strcmp(p_ctx->cache.pos, p_result->pos) != 0),
        .direction_pending = (p_ctx->cache.direction != p_result->direction),
    };

    p_ctx->cache = *p_result;

    return task;
}
