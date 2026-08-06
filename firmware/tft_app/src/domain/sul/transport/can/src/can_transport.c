#include "bsp/can.h"
#include "domain/sul/transport/can.h"

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

/* Хранилище последнего принятого кадра — см. предупреждение в can.h про
 * время жизни p_out->p_data, возвращаемого sul_transport_can_receive(). */
static bsp_can_frame_t s_last_frame;

/* ── Наборы фильтров: раскладка — ДАННЫЕ, применение — общее ────────────────
 *
 * У протоколов на одной шине РАЗНЫЕ раскладки фильтров, а не только разные
 * ID в одной раскладке: НКУ-CAN нужно 7 MB (5 точных PACKET1..5 + 2 wildcard
 * удалённой адресации), УИМ-6100 — ровно ОДИН (ID кадра == адрес индикатора,
 * эталон OLD_PROJECT_TFT4_UIM can.c/set_canrx_id()).
 *
 * Поэтому diff-защита сравнивает ВЕСЬ набор, а не адрес: раньше сравнивался
 * только `last_applied_address`, и переключение протокола НКУ↔УИМ при
 * совпавшем числовом адресе НЕ переприменяло фильтры — раскладка оставалась
 * от прежнего протокола. Плюс перед применением набор снимается целиком
 * (bsp_can_clear_filters): иначе «лишние» MB прежней раскладки остаются
 * активными и пропускают чужие кадры (bsp_can_set_filter только ДОБАВЛЯЕТ).
 */
#define FILTER_SET_MAX 8U

typedef struct
{
    uint32_t id;
    uint32_t mask;
} can_filter_t;

typedef struct
{
    can_filter_t items[FILTER_SET_MAX];
    uint8_t count;
} can_filter_set_t;

/** Что реально запрограммировано в FlexCAN; count == 0 — ничего (форсирует
 *  применение на первый вызов, как сентинел 0xFF в OLD_PROJECT). */
static can_filter_set_t s_applied;

static bool filter_sets_equal(const can_filter_set_t *p_a, const can_filter_set_t *p_b)
{
    if (p_a->count != p_b->count)
    {
        return false;
    }

    for (uint8_t i = 0U; i < p_a->count; i++)
    {
        if ((p_a->items[i].id != p_b->items[i].id) || (p_a->items[i].mask != p_b->items[i].mask))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Применить набор фильтров, если он отличается от запрограммированного.
 *
 * No-op при совпадении — дёшево звать на каждой итерации приёма. При отличии:
 * снять ВСЕ прежние фильтры, затем поставить новые по порядку (MB index = i).
 */
static bsp_status_t apply_filter_set(const can_filter_set_t *p_set)
{
    if ((p_set->count == 0U) || (p_set->count > FILTER_SET_MAX))
    {
        return BSP_ERR_PARAM;
    }

    if (filter_sets_equal(p_set, &s_applied))
    {
        return BSP_OK;
    }

    const bsp_status_t CLR_RC = bsp_can_clear_filters();
    if (CLR_RC != BSP_OK)
    {
        return CLR_RC;
    }
    s_applied.count = 0U; /* прежняя раскладка снята — что бы ни было дальше */

    for (uint8_t i = 0U; i < p_set->count; i++)
    {
        const bsp_status_t RC =
            bsp_can_set_filter(i, p_set->items[i].id, p_set->items[i].mask, false);
        if (RC != BSP_OK)
        {
            return RC;
        }
    }

    s_applied = *p_set;
    return BSP_OK;
}

bsp_status_t sul_transport_can_init(void)
{
    const bsp_can_config_t cfg = { .bitrate = 125000U }; /* см. OLD_PROJECT msg_receiver_task */

    s_applied.count = 0U; /* форсирует применение первого набора фильтров */

    return bsp_can_init(&cfg);
    /* Фильтры не настраиваем здесь — вызывающий (sul_rx_task) обязан сразу
     * позвать set_address СВОЕГО протокола, см. can.h. */
}

bsp_status_t sul_transport_can_set_address_nku(uint8_t nku_address)
{
    const uint8_t ADDR    = (nku_address <= NKU_ADDRESS_MAX) ? nku_address : NKU_ADDRESS_MAX;
    const uint32_t GROUP4 = (uint32_t) ADDR << 4U;
    const uint32_t GROUP6 = (uint32_t) ADDR << 6U;

    const can_filter_set_t SET = {
        .items =
            {
                { PACKET1_BASE | GROUP4, STD_ID_MASK },
                { PACKET2_BASE | GROUP4, STD_ID_MASK },
                { PACKET3_BASE | GROUP4, STD_ID_MASK },
                { PACKET4_BASE | GROUP4, STD_ID_MASK },
                { PACKET5_BASE | GROUP6, STD_ID_MASK },
                /* Wildcard удалённой адресации — от адреса НЕ зависят, но
                 * входят в набор: раскладка описывается одним объектом. */
                { REMOTE_ANNOUNCE_ID, REMOTE_ADDR_ID_MASK },
                { REMOTE_CMD_ID, REMOTE_ADDR_ID_MASK },
            },
        .count = 7U,
    };

    return apply_filter_set(&SET);
}

bsp_status_t sul_transport_can_set_address_uim(uint8_t uim_address)
{
    /* Один точный фильтр: у УИМ CAN ID кадра РАВЕН адресу индикатора
     * (эталон: FLEXCAN_RX_MB_STD_MASK(id, 0, 0) + set_canrx_id()). */
    const can_filter_set_t SET = {
        .items = { { (uint32_t) uim_address, STD_ID_MASK } },
        .count = 1U,
    };

    return apply_filter_set(&SET);
}

bsp_status_t sul_transport_can_send(const sul_tx_frame_t *p_frame, uint32_t timeout_ms)
{
    if ((p_frame == NULL) || (p_frame->len > BSP_CAN_DATA_MAX_LEN))
    {
        return BSP_ERR_PARAM;
    }

    bsp_can_frame_t tx = {
        .id          = p_frame->id,
        .dlc         = p_frame->len,
        .is_extended = false, /* УИМ/НКУ — 11-bit STD */
        .is_remote   = false,
    };

    for (uint8_t i = 0U; i < p_frame->len; i++)
    {
        tx.data[i] = p_frame->data[i];
    }

    return bsp_can_send(&tx, timeout_ms);
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
