#include "domain/sul/transport/demo.h"

#include <stddef.h>

bsp_status_t sul_transport_demo_receive(uint32_t timeout_ms, sul_frame_t *p_out)
{
    (void) timeout_ms;

    p_out->id     = 0U;
    p_out->bus    = 0U;
    p_out->p_data = NULL;
    p_out->len    = 0U;

    return BSP_OK;
}
