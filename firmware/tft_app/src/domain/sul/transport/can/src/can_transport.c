#include "domain/sul/transport/can.h"

#include "bsp/can.h"

/* Базовые ID НКУ-CAN (адрес станции 0) — та же протокольная раскладка, что и
 * decode-сторона (domain/sul/nku_can/src/nku_can.c): PACKET1..4 кодируют
 * адрес в битах [7:4] ID (group4 = addr<<4), PACKET5 — в битах [8:6]
 * (group6 = addr<<6, протокол отводит под него только 3 бита). Decode и
 * transport намеренно НЕ шарят общий заголовок с этими константами — каждый
 * владеет своей копией протокольного факта (декодер ничего не знает про
 * транспорт и наоборот, ARCH). */
#define PACKET1_BASE 0x506U
#define PACKET2_BASE 0x408U
#define PACKET3_BASE 0x508U
#define PACKET4_BASE 0x50BU
#define PACKET5_BASE 0x606U
#define STD_ID_MASK  0x7FFU /* 11-bit STD — проверять все биты */

#define NKU_ADDRESS_MAX 15U /* 4-битный адрес, group4 = addr<<4 */

/* Удалённая установка адреса (§3.5, REMOTE_ADDRES_SETUP): кадры 0x4X1
 * (анонс адреса станции) и 0x5XB (несущая команды) должны приниматься с
 * ЛЮБЫМ X — по определению фичи наш сохранённый адрес мог не совпадать с
 * адресом станции. Точные фильтры PACKET1..5 (маска 0x7FF) такие кадры
 * аппаратно отбрасывают: 0x4X1 не совпадает ни с одним из них вообще, а
 * 0x5XB — только при X == наш адрес (это PACKET4). Поэтому два
 * ДОПОЛНИТЕЛЬНЫХ wildcard-фильтра с маской, игнорирующей адресный нибл
 * [7:4]: проверяются биты [10:8] и [3:0] ID. Порт apply_remote_addr_filters()
 * из OLD_PROJECT (main_programm.c) — БЕЗ них удалённая адресация не работает
 * вовсе (decode до кадров не доходит; найдено на стенде, host-тесты этого не
 * ловят — они кормят decode() напрямую, мимо HW-фильтров). */
#define REMOTE_ADDR_ID_MASK 0x70FU
#define REMOTE_ANNOUNCE_ID  0x401U /* 0x4X1 — анонс адреса станции         */
#define REMOTE_CMD_ID       0x50BU /* 0x5XB — несущая команды (X — любой)  */

/* MB index 0..4 — PACKET1..5 (точные, зависят от адреса);
 * MB index 5..6 — wildcard удалённой адресации (от адреса НЕ зависят). */

/* Хранилище последнего принятого кадра — см. предупреждение в can.h про
 * время жизни p_out->p_data, возвращаемого sul_transport_can_receive(). */
static bsp_can_frame_t s_last_frame;

/* Сентинел вне диапазона 0..15 — форсирует применение фильтров на первый
 * вызов sul_transport_can_set_address(), независимо от переданного адреса
 * (порт OLD_PROJECT msg_receiver_task: last_nku_address = 0xFFU). */
static uint8_t s_last_applied_address = 0xFFU;

bsp_status_t sul_transport_can_init(void)
{
    const bsp_can_config_t cfg = {.bitrate = 125000U}; /* см. OLD_PROJECT msg_receiver_task */

    return bsp_can_init(&cfg);
    /* Фильтры не настраиваем здесь — вызывающий (sul_rx_task) обязан сразу
     * позвать sul_transport_can_set_address(), см. can.h. */
}

bsp_status_t sul_transport_can_set_address(uint8_t nku_address)
{
    const uint8_t ADDR = (nku_address <= NKU_ADDRESS_MAX) ? nku_address : NKU_ADDRESS_MAX;

    if (ADDR == s_last_applied_address)
    {
        return BSP_OK; /* не менялось — переконфигурация MB не нужна */
    }

    const uint32_t GROUP4 = (uint32_t) ADDR << 4U;
    const uint32_t GROUP6 = (uint32_t) ADDR << 6U;

    bsp_status_t st = bsp_can_set_filter(0U, PACKET1_BASE | GROUP4, STD_ID_MASK, false);
    if (st != BSP_OK)
    {
        return st;
    }
    st = bsp_can_set_filter(1U, PACKET2_BASE | GROUP4, STD_ID_MASK, false);
    if (st != BSP_OK)
    {
        return st;
    }
    st = bsp_can_set_filter(2U, PACKET3_BASE | GROUP4, STD_ID_MASK, false);
    if (st != BSP_OK)
    {
        return st;
    }
    st = bsp_can_set_filter(3U, PACKET4_BASE | GROUP4, STD_ID_MASK, false);
    if (st != BSP_OK)
    {
        return st;
    }
    st = bsp_can_set_filter(4U, PACKET5_BASE | GROUP6, STD_ID_MASK, false);
    if (st != BSP_OK)
    {
        return st;
    }

    /* Wildcard-фильтры удалённой адресации (см. блок констант выше). От
     * адреса не зависят — но живут здесь же, а не в init(): применение
     * идемпотентно и дёшево (адрес меняется редко), зато ВСЕ фильтры
     * настраиваются одной функцией в одном месте — нет второй точки входа,
     * которую можно забыть позвать (init() фильтры не трогает намеренно,
     * см. комментарий там). */
    st = bsp_can_set_filter(5U, REMOTE_ANNOUNCE_ID, REMOTE_ADDR_ID_MASK, false);
    if (st != BSP_OK)
    {
        return st;
    }
    st = bsp_can_set_filter(6U, REMOTE_CMD_ID, REMOTE_ADDR_ID_MASK, false);
    if (st != BSP_OK)
    {
        return st;
    }

    s_last_applied_address = ADDR;
    return BSP_OK;
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
