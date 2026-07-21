#include "domain/sul/transport/can.h"

#include "bsp/can.h"

/* Фаза 1: адрес станции захардкожен в 0 — совпадает с базовыми ID в
 * domain/sul/nku_can/src/nku_can.c (0x506|group4, group4=address<<4=0).
 * Фаза 3 параметризует оба конца одновременно из настроек. */
#define PACKET1_ID    0x506U
#define PACKET3_ID    0x508U
#define STD_ID_MASK   0x7FFU /* 11-bit STD — проверять все биты */

/* Хранилище последнего принятого кадра — см. предупреждение в can.h про
 * время жизни p_out->p_data, возвращаемого sul_transport_can_receive(). */
static bsp_can_frame_t s_last_frame;

bsp_status_t sul_transport_can_init(void)
{
    const bsp_can_config_t cfg = {.bitrate = 125000U}; /* см. OLD_PROJECT msg_receiver_task */

    bsp_status_t st = bsp_can_init(&cfg);
    if (st != BSP_OK)
    {
        return st;
    }

    st = bsp_can_set_filter(0U, PACKET1_ID, STD_ID_MASK, false);
    if (st != BSP_OK)
    {
        return st;
    }

    /* Остальные MB (2..15) свободны под Фазу 2 (PACKET2/4/5, remote-address). */
    return bsp_can_set_filter(1U, PACKET3_ID, STD_ID_MASK, false);
}

bsp_status_t sul_transport_can_receive(uint32_t timeout_ms, sul_frame_t *p_out)
{
    const bsp_status_t st = bsp_can_receive(&s_last_frame, timeout_ms);
    if (st != BSP_OK)
    {
        return st;
    }

    p_out->id     = s_last_frame.id;
    p_out->bus    = 0U;
    p_out->p_data = s_last_frame.data;
    p_out->len    = s_last_frame.dlc;

    return BSP_OK;
}
