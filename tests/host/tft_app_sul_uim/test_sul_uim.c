/**
 * @file  test_sul_uim.c
 * @brief Host unit-тесты декодера УИМ-6100: гейтинг кадра, коды этажей,
 *        сигналы, роли адресов и ГИСТЕРЕЗИС удержания спецрежимов.
 *        См. README.md.
 */

#include "domain/sul/uim.h"
#include "unity.h"

#include <string.h>

/* ── Раскладка кадра (эталон OLD_PROJECT_TFT4_UIM) ────────────────────────────
 * data[0..1] — заголовок 0x81 0x00
 * data[2]    — W_0
 * data[3]    — W_1: код сообщения
 * data[4]    — W_2: код этажа ЛИБО код спецрежима
 * data[5]    — W_3: направление [1:0], гонг 0x04, движение 0x10, отклик 0x80
 */
#define UIM_DLC 6U

#define W3_ARROW_NONE   0x00U
#define W3_ARROW_DOWN   0x01U
#define W3_ARROW_UP     0x02U
#define W3_ARROW_DOUBLE 0x03U
#define W3_ARRIVAL      0x04U
#define W3_MOVEMENT     0x10U
/** Бит отклика АКТИВЕН НУЛЁМ: установленный бит = отклик НЕ нужен. */
#define W3_NO_RESPONSE 0x80U

/* Коды этажей / спецрежимов (W_2). */
#define FC_FLOOR_0     0U
#define FC_SUBFLOOR_1  41U
#define FC_SUBFLOOR_9  49U
#define FC_RESERVED    50U
#define FC_SEISMIC     51U
#define FC_OUT_SERV_1  52U
#define FC_FIREMAN     53U
#define FC_OUT_SERV_2  54U
#define FC_MAINTENANCE 55U
#define FC_EVACUATION  56U
#define FC_FIRE_ALARM  57U
#define FC_FAILURE     58U
#define FC_LADING      59U

/** Код сообщения «Перегруз» (W_1) — единственный, влияющий на индикацию. */
#define MC_OVERLOAD 0x39U

/* Адреса-роли. */
#define ADDR_CABIN      46U
#define ADDR_MAIN_FLOOR 47U
#define ADDR_AUX_MAIN   48U
#define ADDR_UNIVERSAL  49U
#define ADDR_CABIN_2    50U
#define ADDR_FLOOR_UNIT 7U

/**
 * Гистерезис: ATTACK=+6 за кадр спецрежима, RELEASE=-2 за кадр обычного
 * этажа → ровно 3 «этажных» кадра гасят режим, взведённый ОДНИМ. Потолок 500
 * → не более 250 кадров на выход из долго висящего режима.
 */
#define RELEASE_FRAMES_PER_ATTACK 3U
#define CEILING_RELEASE_FRAMES    250U

static uim_ctx_t g_ctx;

void setUp(void)
{
    uim_init(&g_ctx);
}

void tearDown(void)
{
}

/* ── Конструкторы кадров ─────────────────────────────────────────────────── */

static sul_frame_t make_frame(uint32_t id, uint8_t w1_msg, uint8_t w2_floor, uint8_t w3_signals)
{
    static uint8_t s_data[8];
    memset(s_data, 0, sizeof(s_data));
    s_data[0] = 0x81U;
    s_data[1] = 0x00U;
    s_data[2] = 0x00U;
    s_data[3] = w1_msg;
    s_data[4] = w2_floor;
    s_data[5] = w3_signals;
    return (sul_frame_t){ .id = id, .bus = 0U, .p_data = s_data, .len = UIM_DLC };
}

/** Кадр обычного этажа на дефолтный адрес (без кода сообщения/сигналов). */
static sul_frame_t floor_frame(uint8_t floor_code)
{
    return make_frame(ADDR_CABIN, 0U, floor_code, W3_ARROW_NONE);
}

/** Кадр спецрежима на дефолтный адрес. */
static sul_frame_t mode_frame(uint8_t mode_code)
{
    return make_frame(ADDR_CABIN, 0U, mode_code, W3_ARROW_NONE);
}

static sul_result_t decode_ok(const sul_frame_t *p_frame)
{
    sul_result_t out;
    TEST_ASSERT_EQUAL(SUL_STATUS_OK, uim_decode(&g_ctx, p_frame, &out));
    return out;
}

/** Прогнать n кадров обычного этажа; вернуть последний результат. */
static sul_result_t feed_floor_frames(uint8_t floor_code, unsigned n)
{
    sul_result_t out = { 0 };
    for (unsigned i = 0U; i < n; i++)
    {
        const sul_frame_t FRAME = floor_frame(floor_code);
        out                     = decode_ok(&FRAME);
    }
    return out;
}

/* ── Инициализация ───────────────────────────────────────────────────────── */

static void test_init_sets_cabin_address_and_clears_hysteresis(void)
{
    TEST_ASSERT_EQUAL_UINT8(ADDR_CABIN, g_ctx.uim_address);
    TEST_ASSERT_EQUAL_UINT16(0U, g_ctx.special_mode_cnt);
    TEST_ASSERT_EQUAL_STRING("--", g_ctx.state.pos);
}

static void test_set_address_clamps_above_max(void)
{
    uim_set_address(&g_ctx, 200U);
    TEST_ASSERT_EQUAL_UINT8(50U, g_ctx.uim_address);
}

/* ── Гейтинг кадра ───────────────────────────────────────────────────────── */

static void test_frame_for_other_address_ignored(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_MAIN_FLOOR, 0U, 5U, W3_ARROW_NONE);
    sul_result_t out;
    TEST_ASSERT_EQUAL(SUL_STATUS_IGNORED, uim_decode(&g_ctx, &FRAME, &out));
}

static void test_our_id_with_wrong_dlc_is_err(void)
{
    sul_frame_t frame = floor_frame(5U);
    frame.len         = 8U;
    sul_result_t out;
    TEST_ASSERT_EQUAL(SUL_STATUS_ERR, uim_decode(&g_ctx, &frame, &out));
}

static void test_non_info_header_ignored(void)
{
    sul_frame_t frame = floor_frame(5U);
    ((uint8_t *) frame.p_data)[0] = 0x82U; /* не 0x81 — другой класс сообщения */
    sul_result_t out;
    TEST_ASSERT_EQUAL(SUL_STATUS_IGNORED, uim_decode(&g_ctx, &frame, &out));
}

/* ── Коды этажей ─────────────────────────────────────────────────────────── */

static void test_floor_zero(void)
{
    const sul_frame_t FRAME = floor_frame(FC_FLOOR_0);
    TEST_ASSERT_EQUAL_STRING("0", decode_ok(&FRAME).pos);
}

static void test_floor_single_digit(void)
{
    const sul_frame_t FRAME = floor_frame(7U);
    TEST_ASSERT_EQUAL_STRING("7", decode_ok(&FRAME).pos);
}

static void test_floor_two_digits(void)
{
    const sul_frame_t FRAME = floor_frame(23U);
    TEST_ASSERT_EQUAL_STRING("23", decode_ok(&FRAME).pos);
}

static void test_floor_max_positive(void)
{
    const sul_frame_t FRAME = floor_frame(40U);
    TEST_ASSERT_EQUAL_STRING("40", decode_ok(&FRAME).pos);
}

static void test_subfloor_minus_one(void)
{
    const sul_frame_t FRAME = floor_frame(FC_SUBFLOOR_1);
    TEST_ASSERT_EQUAL_STRING("-1", decode_ok(&FRAME).pos);
}

static void test_subfloor_minus_nine(void)
{
    const sul_frame_t FRAME = floor_frame(FC_SUBFLOOR_9);
    TEST_ASSERT_EQUAL_STRING("-9", decode_ok(&FRAME).pos);
}

/** Код 50 — резерв: в эталоне ветки нет, состояние не меняется. */
static void test_reserved_code_leaves_position_intact(void)
{
    const sul_frame_t FLOOR = floor_frame(12U);
    (void) decode_ok(&FLOOR);

    const sul_frame_t RESERVED = floor_frame(FC_RESERVED);
    TEST_ASSERT_EQUAL_STRING("12", decode_ok(&RESERVED).pos);
}

/* ── Сигналы W_3 ─────────────────────────────────────────────────────────── */

static void test_direction_none(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_NONE);
    TEST_ASSERT_EQUAL(SUL_DIR_NONE, decode_ok(&FRAME).direction);
}

static void test_direction_down(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_DOWN);
    TEST_ASSERT_EQUAL(SUL_DIR_DOWN, decode_ok(&FRAME).direction);
}

static void test_direction_up(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_UP);
    TEST_ASSERT_EQUAL(SUL_DIR_UP, decode_ok(&FRAME).direction);
}

static void test_direction_double(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_DOUBLE);
    TEST_ASSERT_EQUAL(SUL_DIR_DOUBLE, decode_ok(&FRAME).direction);
}

static void test_arrival_and_movement_bits(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, (uint8_t) (W3_ARRIVAL | W3_MOVEMENT));
    const sul_result_t R    = decode_ok(&FRAME);
    TEST_ASSERT_TRUE(R.arrival);
    TEST_ASSERT_TRUE(R.movement);
}

static void test_arrival_and_movement_clear_when_bits_absent(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_UP);
    const sul_result_t R    = decode_ok(&FRAME);
    TEST_ASSERT_FALSE(R.arrival);
    TEST_ASSERT_FALSE(R.movement);
}

/* ── Роли адресов и отклик ───────────────────────────────────────────────── */

static void test_cop_mode_true_for_cabin_addresses(void)
{
    const uint8_t CABIN_ADDRESSES[] = { ADDR_CABIN, ADDR_CABIN_2 };

    for (unsigned i = 0U; i < sizeof(CABIN_ADDRESSES) / sizeof(CABIN_ADDRESSES[0]); i++)
    {
        uim_init(&g_ctx);
        uim_set_address(&g_ctx, CABIN_ADDRESSES[i]);
        const sul_frame_t FRAME = make_frame(CABIN_ADDRESSES[i], 0U, 5U, W3_ARROW_NONE);
        (void) decode_ok(&FRAME);
        TEST_ASSERT_TRUE(g_ctx.cop_mode);
    }
}

static void test_cop_mode_false_for_floor_addresses(void)
{
    const uint8_t FLOOR_ADDRESSES[] = { ADDR_MAIN_FLOOR, ADDR_AUX_MAIN, ADDR_UNIVERSAL,
                                        ADDR_FLOOR_UNIT };

    for (unsigned i = 0U; i < sizeof(FLOOR_ADDRESSES) / sizeof(FLOOR_ADDRESSES[0]); i++)
    {
        uim_init(&g_ctx);
        uim_set_address(&g_ctx, FLOOR_ADDRESSES[i]);
        const sul_frame_t FRAME = make_frame(FLOOR_ADDRESSES[i], 0U, 5U, W3_ARROW_NONE);
        (void) decode_ok(&FRAME);
        TEST_ASSERT_FALSE(g_ctx.cop_mode);
    }
}

/** Бит отклика активен НУЛЁМ — сброшенный бит означает «отклик нужен». */
static void test_response_needed_when_bit_clear(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_NONE);
    (void) decode_ok(&FRAME);
    TEST_ASSERT_TRUE(g_ctx.should_respond);
}

static void test_response_not_needed_when_bit_set(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_NO_RESPONSE);
    (void) decode_ok(&FRAME);
    TEST_ASSERT_FALSE(g_ctx.should_respond);
}

/** Отклик обязателен только для ролей 46/47/48; для прочих — никогда. */
static void test_response_never_for_other_roles(void)
{
    const uint8_t SILENT_ADDRESSES[] = { ADDR_UNIVERSAL, ADDR_CABIN_2, ADDR_FLOOR_UNIT };

    for (unsigned i = 0U; i < sizeof(SILENT_ADDRESSES) / sizeof(SILENT_ADDRESSES[0]); i++)
    {
        uim_init(&g_ctx);
        uim_set_address(&g_ctx, SILENT_ADDRESSES[i]);
        /* Бит сброшен — для 46/47/48 это означало бы «отклик нужен». */
        const sul_frame_t FRAME = make_frame(SILENT_ADDRESSES[i], 0U, 5U, W3_ARROW_NONE);
        (void) decode_ok(&FRAME);
        TEST_ASSERT_FALSE(g_ctx.should_respond);
    }
}

/* ── Спецрежимы: отображение кода на флаг ────────────────────────────────── */

static void test_mode_code_maps_to_flag(void)
{
    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    TEST_ASSERT_TRUE(decode_ok(&FIRE).fire_alarm);

    uim_init(&g_ctx);
    const sul_frame_t SEISMIC = mode_frame(FC_SEISMIC);
    TEST_ASSERT_TRUE(decode_ok(&SEISMIC).seismic);

    uim_init(&g_ctx);
    const sul_frame_t FIREMAN = mode_frame(FC_FIREMAN);
    TEST_ASSERT_TRUE(decode_ok(&FIREMAN).fireman);

    uim_init(&g_ctx);
    const sul_frame_t MAINT = mode_frame(FC_MAINTENANCE);
    TEST_ASSERT_TRUE(decode_ok(&MAINT).maintenance);

    uim_init(&g_ctx);
    const sul_frame_t LADING = mode_frame(FC_LADING);
    TEST_ASSERT_TRUE(decode_ok(&LADING).lading);
}

static void test_out_of_service_codes_map_to_error(void)
{
    const uint8_t ERROR_CODES[] = { FC_OUT_SERV_1, FC_OUT_SERV_2, FC_FAILURE };

    for (unsigned i = 0U; i < sizeof(ERROR_CODES) / sizeof(ERROR_CODES[0]); i++)
    {
        uim_init(&g_ctx);
        const sul_frame_t FRAME = mode_frame(ERROR_CODES[i]);
        TEST_ASSERT_TRUE(decode_ok(&FRAME).error);
    }
}

/** Двух спецрежимов одновременно не бывает — последний пришедший побеждает. */
static void test_mode_last_one_wins(void)
{
    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    (void) decode_ok(&FIRE);

    const sul_frame_t MAINT = mode_frame(FC_MAINTENANCE);
    const sul_result_t R    = decode_ok(&MAINT);

    TEST_ASSERT_FALSE(R.fire_alarm);
    TEST_ASSERT_TRUE(R.maintenance);
}

static void test_evacuation_mode(void)
{
    const sul_frame_t FRAME = mode_frame(FC_EVACUATION);
    const sul_result_t R    = decode_ok(&FRAME);

    TEST_ASSERT_TRUE(R.evacuation);
    TEST_ASSERT_NOT_EQUAL_UINT16(0U, g_ctx.special_mode_cnt);
}

/** Эвакуация гаснет тем же гистерезисом, что и остальные режимы. */
static void test_evacuation_released_by_normal_frames(void)
{
    const sul_frame_t FRAME = mode_frame(FC_EVACUATION);
    (void) decode_ok(&FRAME);

    const sul_result_t R = feed_floor_frames(5U, RELEASE_FRAMES_PER_ATTACK);
    TEST_ASSERT_FALSE(R.evacuation);
}

/* ── Исходящий отклик станции (take_pending_tx) ──────────────────────────── */

/** Роли 46/47/48 при СБРОШЕННОМ бите W_3.7 обязаны ответить `0x81 0x00`. */
static void test_pending_tx_builds_response_frame(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_NONE);
    (void) decode_ok(&FRAME);

    sul_tx_frame_t tx;
    TEST_ASSERT_TRUE(uim_take_pending_tx(&g_ctx, &tx));

    TEST_ASSERT_EQUAL_UINT32(ADDR_CABIN + 0x80U, tx.id); /* адрес + 0x80 */
    TEST_ASSERT_EQUAL_UINT8(2U, tx.len);
    TEST_ASSERT_EQUAL_HEX8(0x81U, tx.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, tx.data[1]);
}

static void test_pending_tx_absent_when_response_bit_set(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_NO_RESPONSE);
    (void) decode_ok(&FRAME);

    sul_tx_frame_t tx;
    TEST_ASSERT_FALSE(uim_take_pending_tx(&g_ctx, &tx));
}

/** Один отклик на кадр: повторный take НЕ отдаёт тот же кадр снова. */
static void test_pending_tx_is_one_shot(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_NONE);
    (void) decode_ok(&FRAME);

    sul_tx_frame_t tx;
    TEST_ASSERT_TRUE(uim_take_pending_tx(&g_ctx, &tx));
    TEST_ASSERT_FALSE(uim_take_pending_tx(&g_ctx, &tx));
}

/**
 * Запрос ТРАНЗИТЕН (контракт sul_take_pending_tx_fn_t): кадр, отброшенный
 * гейтом, обязан погасить отклик от предыдущего — иначе устаревший отклик
 * уедет в шину повторно.
 */
static void test_pending_tx_cleared_by_ignored_frame(void)
{
    const sul_frame_t NEEDS_RESPONSE = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_NONE);
    (void) decode_ok(&NEEDS_RESPONSE);

    /* Кадр на чужой адрес — IGNORED, отклик должен сброситься. */
    const sul_frame_t OTHER = make_frame(ADDR_MAIN_FLOOR, 0U, 5U, W3_ARROW_NONE);
    sul_result_t out;
    TEST_ASSERT_EQUAL(SUL_STATUS_IGNORED, uim_decode(&g_ctx, &OTHER, &out));

    sul_tx_frame_t tx;
    TEST_ASSERT_FALSE(uim_take_pending_tx(&g_ctx, &tx));
}

/** Роли вне 46/47/48 не отвечают никогда — отклика нет и в исходящем канале. */
static void test_pending_tx_absent_for_silent_roles(void)
{
    uim_set_address(&g_ctx, ADDR_CABIN_2);

    const sul_frame_t FRAME = make_frame(ADDR_CABIN_2, 0U, 5U, W3_ARROW_NONE);
    (void) decode_ok(&FRAME);

    sul_tx_frame_t tx;
    TEST_ASSERT_FALSE(uim_take_pending_tx(&g_ctx, &tx));
}

/** ID отклика следует за адресом индикатора, а не захардкожен под 46. */
static void test_pending_tx_id_follows_address(void)
{
    uim_set_address(&g_ctx, ADDR_AUX_MAIN);

    const sul_frame_t FRAME = make_frame(ADDR_AUX_MAIN, 0U, 5U, W3_ARROW_NONE);
    (void) decode_ok(&FRAME);

    sul_tx_frame_t tx;
    TEST_ASSERT_TRUE(uim_take_pending_tx(&g_ctx, &tx));
    TEST_ASSERT_EQUAL_UINT32(ADDR_AUX_MAIN + 0x80U, tx.id);
}

/* ── ГИСТЕРЕЗИС ──────────────────────────────────────────────────────────── */

/** Один кадр взводит режим. */
static void test_mode_asserts_on_single_frame(void)
{
    const sul_frame_t FRAME = mode_frame(FC_FIRE_ALARM);
    TEST_ASSERT_TRUE(decode_ok(&FRAME).fire_alarm);
}

/** Двух «этажных» кадров НЕ хватает, чтобы погасить режим (нужно три). */
static void test_mode_still_held_after_two_normal_frames(void)
{
    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    (void) decode_ok(&FIRE);

    const sul_result_t R = feed_floor_frames(5U, RELEASE_FRAMES_PER_ATTACK - 1U);
    TEST_ASSERT_TRUE(R.fire_alarm);
}

/** Третий «этажный» кадр гасит режим — и гасит ВСЕ флаги разом. */
static void test_mode_released_after_three_normal_frames(void)
{
    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    (void) decode_ok(&FIRE);

    const sul_result_t R = feed_floor_frames(5U, RELEASE_FRAMES_PER_ATTACK);
    TEST_ASSERT_FALSE(R.fire_alarm);
    TEST_ASSERT_EQUAL_UINT16(0U, g_ctx.special_mode_cnt);
}

/**
 * САМЫЙ ВАЖНЫЙ КЕЙС — ради него гистерезис и существует. Станция ЧЕРЕДУЕТ
 * кадры «режим / этаж»: без удержания индикация мигала бы с частотой шины.
 */
static void test_alternating_mode_and_floor_frames_hold_mode(void)
{
    for (unsigned i = 0U; i < 50U; i++)
    {
        const sul_frame_t MODE = mode_frame(FC_FIRE_ALARM);
        TEST_ASSERT_TRUE(decode_ok(&MODE).fire_alarm);

        const sul_frame_t FLOOR = floor_frame(5U);
        TEST_ASSERT_TRUE(decode_ok(&FLOOR).fire_alarm); /* НЕ мигнул */
    }
}

/** Позиция продолжает обновляться «под» удерживаемым режимом (как в эталоне). */
static void test_position_keeps_updating_while_mode_held(void)
{
    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    (void) decode_ok(&FIRE);

    const sul_frame_t FLOOR = floor_frame(9U);
    const sul_result_t R    = decode_ok(&FLOOR);

    TEST_ASSERT_TRUE(R.fire_alarm);
    TEST_ASSERT_EQUAL_STRING("9", R.pos);
}

/**
 * Направление обновляется и ПОД удерживаемым режимом — режим и стрелка это
 * независимые поля модели, layout выводит их одновременно. В эталоне стрелка
 * замораживалась, т.к. делила слот вывода с иконкой режима; здесь такой
 * заморозки НЕТ осознанно (см. блок про гистерезис в uim.c).
 */
static void test_direction_keeps_updating_while_mode_held(void)
{
    const sul_frame_t UP = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_UP);
    TEST_ASSERT_EQUAL(SUL_DIR_UP, decode_ok(&UP).direction);

    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    (void) decode_ok(&FIRE);

    /* Кадр обычного этажа со стрелкой ВНИЗ — режим ещё держится (cnt 6→4). */
    const sul_frame_t DOWN = make_frame(ADDR_CABIN, 0U, 5U, W3_ARROW_DOWN);
    const sul_result_t R   = decode_ok(&DOWN);

    TEST_ASSERT_TRUE(R.fire_alarm);               /* режим держится...        */
    TEST_ASSERT_EQUAL(SUL_DIR_DOWN, R.direction); /* ...а стрелка уже свежая  */
}

/** Кадр спецрежима тоже несёт W_3 — его стрелка применяется, не игнорируется. */
static void test_direction_from_mode_frame_is_applied(void)
{
    const sul_frame_t MODE_WITH_ARROW = make_frame(ADDR_CABIN, 0U, FC_FIRE_ALARM, W3_ARROW_UP);
    const sul_result_t R              = decode_ok(&MODE_WITH_ARROW);

    TEST_ASSERT_TRUE(R.fire_alarm);
    TEST_ASSERT_EQUAL(SUL_DIR_UP, R.direction);
}

/** Гонг/движение читаются ВСЕГДА, даже пока режим держится. */
static void test_arrival_still_decoded_while_mode_held(void)
{
    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    (void) decode_ok(&FIRE);

    const sul_frame_t GONG = make_frame(ADDR_CABIN, 0U, 5U, W3_ARRIVAL);
    const sul_result_t R   = decode_ok(&GONG);

    TEST_ASSERT_TRUE(R.fire_alarm);
    TEST_ASSERT_TRUE(R.arrival);
}

/* ── Перегруз (код сообщения) ────────────────────────────────────────────── */

static void test_overload_from_message_code(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, MC_OVERLOAD, 5U, W3_ARROW_NONE);
    TEST_ASSERT_TRUE(decode_ok(&FRAME).overload);
}

/**
 * Перегруз в кадре С обычным этажом: ATTACK(+6) идёт ДО RELEASE(−2) — итог
 * +4, режим держится. Порядок вызовов в decode() значим, тест его фиксирует.
 */
static void test_overload_with_normal_floor_still_holds(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, MC_OVERLOAD, 5U, W3_ARROW_NONE);
    const sul_result_t R    = decode_ok(&FRAME);

    TEST_ASSERT_TRUE(R.overload);
    TEST_ASSERT_NOT_EQUAL_UINT16(0U, g_ctx.special_mode_cnt);
}

static void test_overload_released_by_normal_frames(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, MC_OVERLOAD, 5U, W3_ARROW_NONE);
    (void) decode_ok(&FRAME);

    /* +4 после кадра выше → два «этажных» кадра гасят. */
    const sul_result_t R = feed_floor_frames(5U, 2U);
    TEST_ASSERT_FALSE(R.overload);
}

/** Перегруз и режим из кода этажа — независимые владельцы, живут вместе. */
static void test_overload_and_floor_code_mode_coexist(void)
{
    const sul_frame_t FRAME = make_frame(ADDR_CABIN, MC_OVERLOAD, FC_FIRE_ALARM, W3_ARROW_NONE);
    const sul_result_t R    = decode_ok(&FRAME);

    TEST_ASSERT_TRUE(R.overload);
    TEST_ASSERT_TRUE(R.fire_alarm);
}

/* ── Насыщение потолком ──────────────────────────────────────────────────── */

/**
 * Долго висящий режим не должен требовать неограниченного числа кадров на
 * выход: счётчик насыщается на 500 → не более 250 «этажных» кадров.
 */
static void test_long_mode_releases_within_ceiling_bound(void)
{
    for (unsigned i = 0U; i < 1000U; i++)
    {
        const sul_frame_t MODE = mode_frame(FC_FIRE_ALARM);
        (void) decode_ok(&MODE);
    }

    const sul_result_t HELD = feed_floor_frames(5U, CEILING_RELEASE_FRAMES - 1U);
    TEST_ASSERT_TRUE(HELD.fire_alarm); /* потолок реально достигнут */

    const sul_result_t RELEASED = feed_floor_frames(5U, 1U);
    TEST_ASSERT_FALSE(RELEASED.fire_alarm);
}

/** Подземные этажи тоже отпускают удержание (не только положительные). */
static void test_subfloor_frames_release_mode(void)
{
    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    (void) decode_ok(&FIRE);

    const sul_result_t R = feed_floor_frames(FC_SUBFLOOR_1, RELEASE_FRAMES_PER_ATTACK);
    TEST_ASSERT_FALSE(R.fire_alarm);
    TEST_ASSERT_EQUAL_STRING("-1", R.pos);
}

/** Резервный код 50 гистерезис НЕ двигает (ни ATTACK, ни RELEASE). */
static void test_reserved_code_does_not_move_hysteresis(void)
{
    const sul_frame_t FIRE = mode_frame(FC_FIRE_ALARM);
    (void) decode_ok(&FIRE);
    const uint16_t AFTER_ATTACK = g_ctx.special_mode_cnt;

    for (unsigned i = 0U; i < 10U; i++)
    {
        const sul_frame_t RESERVED = floor_frame(FC_RESERVED);
        (void) decode_ok(&RESERVED);
    }

    TEST_ASSERT_EQUAL_UINT16(AFTER_ATTACK, g_ctx.special_mode_cnt);
    TEST_ASSERT_TRUE(g_ctx.state.fire_alarm);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_sets_cabin_address_and_clears_hysteresis);
    RUN_TEST(test_set_address_clamps_above_max);

    RUN_TEST(test_frame_for_other_address_ignored);
    RUN_TEST(test_our_id_with_wrong_dlc_is_err);
    RUN_TEST(test_non_info_header_ignored);

    RUN_TEST(test_floor_zero);
    RUN_TEST(test_floor_single_digit);
    RUN_TEST(test_floor_two_digits);
    RUN_TEST(test_floor_max_positive);
    RUN_TEST(test_subfloor_minus_one);
    RUN_TEST(test_subfloor_minus_nine);
    RUN_TEST(test_reserved_code_leaves_position_intact);

    RUN_TEST(test_direction_none);
    RUN_TEST(test_direction_down);
    RUN_TEST(test_direction_up);
    RUN_TEST(test_direction_double);
    RUN_TEST(test_arrival_and_movement_bits);
    RUN_TEST(test_arrival_and_movement_clear_when_bits_absent);

    RUN_TEST(test_cop_mode_true_for_cabin_addresses);
    RUN_TEST(test_cop_mode_false_for_floor_addresses);
    RUN_TEST(test_response_needed_when_bit_clear);
    RUN_TEST(test_response_not_needed_when_bit_set);
    RUN_TEST(test_response_never_for_other_roles);

    RUN_TEST(test_mode_code_maps_to_flag);
    RUN_TEST(test_out_of_service_codes_map_to_error);
    RUN_TEST(test_mode_last_one_wins);
    RUN_TEST(test_evacuation_mode);
    RUN_TEST(test_evacuation_released_by_normal_frames);

    RUN_TEST(test_pending_tx_builds_response_frame);
    RUN_TEST(test_pending_tx_absent_when_response_bit_set);
    RUN_TEST(test_pending_tx_is_one_shot);
    RUN_TEST(test_pending_tx_cleared_by_ignored_frame);
    RUN_TEST(test_pending_tx_absent_for_silent_roles);
    RUN_TEST(test_pending_tx_id_follows_address);

    RUN_TEST(test_mode_asserts_on_single_frame);
    RUN_TEST(test_mode_still_held_after_two_normal_frames);
    RUN_TEST(test_mode_released_after_three_normal_frames);
    RUN_TEST(test_alternating_mode_and_floor_frames_hold_mode);
    RUN_TEST(test_position_keeps_updating_while_mode_held);
    RUN_TEST(test_direction_keeps_updating_while_mode_held);
    RUN_TEST(test_direction_from_mode_frame_is_applied);
    RUN_TEST(test_arrival_still_decoded_while_mode_held);

    RUN_TEST(test_overload_from_message_code);
    RUN_TEST(test_overload_with_normal_floor_still_holds);
    RUN_TEST(test_overload_released_by_normal_frames);
    RUN_TEST(test_overload_and_floor_code_mode_coexist);

    RUN_TEST(test_long_mode_releases_within_ceiling_bound);
    RUN_TEST(test_subfloor_frames_release_mode);
    RUN_TEST(test_reserved_code_does_not_move_hysteresis);

    return UNITY_END();
}
