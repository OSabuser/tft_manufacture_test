#include "domain/sul/uim.h"

#include <stdbool.h>
#include <stdio.h>

#define PROTO_DLC 6U

#define UIM_ADDRESS_MAX 50U /* адрес 0..15; group4 = addr<<4 */

#define MSG_CODE_VOICE_MOVING_UP        0x33
#define MSG_CODE_VOICE_MOVING_DOWN      0x34
#define MSG_CODE_VOICE_ELEVATOR_FAILURE 0x37
#define MSG_CODE_VOICE_FIRE_ALARM       0x38
#define MSG_CODE_VOICE_OVERLOAD         0x39
#define MSG_CODE_VOICE_DOORS_CLOSING    0x3A
#define MSG_CODE_VOICE_DOORS_OPENING    0x3B
#define MSG_CODE_SOUND_DOORS_CLOSING    0x3C
#define MSG_CODE_SOUND_DOORS_OPENING    0x3D
#define MSG_CODE_SOUND_BUTTON_PRESSING  0x3E

#define FLOOR_CODE_SUBFLOOR_ONE 0x29
#define FLOOR_CODE_SUBFLOOR_NINE                                                                   \
    0x31 /* Максимальное значение кода этажа, относящееся к номеру этажа (соответствует "-9")*/
#define FLOOR_CODE_RESERVED         0x32 /* Резерв */
#define FLOOR_CODE_SEISMIC_HAZARD   0x33 /* Сейсмо-опасность */
#define FLOOR_CODE_OUT_OF_SERVICE_1 0x34 /* Лифт не работает */
#define FLOOR_CODE_FIREMAN          0x35 /* Перевозка пожарных подразделений */
#define FLOOR_CODE_OUT_OF_SERVICE_2 0x36 /* Лифт не работает */
#define FLOOR_CODE_MAINTENANCE      0x37 /* Сервисное обслуживания */
#define FLOOR_CODE_EVACUATION       0x38 /* Эвакуация */
#define FLOOR_CODE_FIRE_ALARM       0x39 /* Пожарная опасность */
#define FLOOR_CODE_FAILURE          0x3A /* Неисправность ИБП */
#define FLOOR_CODE_LADING           0x3B /* Погрузка */

#define ARROW_MASK        0x03U /* Направление движения - W_3 */
#define MESSAGE_CODE_MASK 0x3FU /* Код сообщения - W_1 */
#define FLOOR_MASK        0x3FU /* Номер этажа - W_2 */

#define ARRIVAL_SIGNAL_BIT_MASK  0x04U /* Бит включения сигнала "Гонг" */
#define ALARM_SIGNAL_BIT_MASK    0x08U /* Бит включения сигнала "Сирена" */
#define MOVEMENT_BIT_MASK        0x10U /* Бит признака наличия движения кабины */
#define SPEECH_ENABLED_BIT_MASK  0x20U /* Бит включения речевого сопровождения */
#define RESPONSE_NEEDED_BIT_MASK 0x80U /* Бит необходимости ответа СУЛ */

#define SYMBOL_SPACE  16U
#define SYMBOL_A      10U
#define SYMBOL_P      17U /* "П" */
#define SYMBOL_p      19U /* "п" */
#define SYMBOL_HYPHEN 22U /* "-" */
#define SYMBOL_TOTAL  38U

#define CABIN_INDICATOR_ID           46U
#define MAIN_FLOOR_INDICATOR_ID      47U
#define AUX_MAIN_FLOOR_INDICATOR_ID  48U
#define UNIVERSAL_FLOOR_INDICATOR_ID 49U
#define SECONDARY_CABIN_INDICATOR_ID 50U

/* Таблица символов НКУ-CAN — порт из OLD_PROJECT floor_string_composer()
 * (source/main_programm.c). Индекс — код символа с шины (байт & FLOOR_MASK). */
static const char *const S_SYMBOL_TABLE[SYMBOL_TOTAL] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "b", "C", "d", "E", "F", " ", "П", "Р",
    "п", "H", "U", "-", "_", "u", "L", "У", "Б", "Г", "R", "V", "N", "S", "K", "Y", "G", "B", "T",
};

void nku_can_init(uim_ctx_t *p_ctx)
{
    p_ctx->state = sul_default_state();

    p_ctx->actual_floor_number = 0U;
    p_ctx->uim_address    = 46U; /* По умолчанию: индикатор в кабине */
    p_ctx->cop_mode       = false;
    p_ctx->should_respond = false;
}

void uim_can_set_address(uim_ctx_t *p_ctx, uint8_t uim_address)
{
    p_ctx->uim_address = (uim_address <= UIM_ADDRESS_MAX) ? uim_address : UIM_ADDRESS_MAX;
}

/**
 * @brief Производный числовой этаж для озвучки — порт floor_number_parser().
 *
 * Стандартные (0..40)
 * Отрицательные: (41..50)
 * Всё нераспознанное / вне диапазона → FLOOR_NUM_UNKNOWN (60).
 */
static uint8_t floor_number_parser(uint8_t left, uint8_t right)
{
    //TODO: доделать
    return 0;
}

#if 0
static void decode_packet1(nku_can_ctx_t *p_ctx, const uint8_t *p_data)
{
    p_ctx->state.direction = (sul_direction_t) (p_data[6] & ARROW_MASK);
    p_ctx->state.movement  = ((p_data[6] & MOVEMENT_MASK) != 0U);
    p_ctx->current_level   = p_data[3] & FLOOR_MASK;

    /* Стрелка «нет движения» → сбрасываем следующий этаж (появляется только
     * пока кабина едет, гаснет на прибытии — см. PACKET5). */
    if (p_ctx->state.direction == SUL_DIR_NONE)
    {
        p_ctx->state.next[0] = '\0';
    }

    p_ctx->state.fire_alarm  = false;
    p_ctx->state.maintenance = false;
    p_ctx->state.fireman     = false;
    p_ctx->lading_instr      = false;

    switch (p_data[6] & ICON_MASK)
    {
    case ICON_FIRE:
        p_ctx->state.fire_alarm = true;
        break;
    case ICON_MP1:
    case ICON_MP2:
    case ICON_REVISION:
        p_ctx->state.maintenance = true;
        break;
    case ICON_LADING:
        p_ctx->lading_instr = true;
        break;
    case ICON_FIREMAN:
        p_ctx->state.fireman = true;
        break;
    default:
        break;
    }
}
#endif

// Байт W[2]: код этажа
static void decode_floor_code(uim_ctx_t *p_ctx, const uint8_t *p_data)
{
    const uint8_t FLOOR_CODE = p_data[4] & FLOOR_MASK;

    if (FLOOR_CODE > FLOOR_CODE_SUBFLOOR_NINE)
    {

        // Следует интерпретировать код этажа как режим
        switch (FLOOR_CODE)
        {
        case FLOOR_CODE_FIRE_ALARM:
            p_ctx->state.fire_alarm = true;
            break;
        case FLOOR_CODE_MAINTENANCE:
            p_ctx->state.maintenance = true;
            break;
        case FLOOR_CODE_LADING:
            p_ctx->state.lading = true;
            break;
        case FLOOR_CODE_FIREMAN:
            p_ctx->state.fireman = true;
            break;
        case FLOOR_CODE_FAILURE:
        case FLOOR_CODE_OUT_OF_SERVICE_1:
        case FLOOR_CODE_OUT_OF_SERVICE_2:
            p_ctx->state.error = true;
            break;
        default:
            //FIXME: в будущем сделать обработку других режимов и ввести счетчики
            p_ctx->state.fire_alarm  = false;
            p_ctx->state.maintenance = false;
            p_ctx->state.lading      = false;
            p_ctx->state.fireman     = false;
            p_ctx->state.error       = false;
            break;
        }
        return;
    }

    // Номера этажей от "-1" до "-9"
    if ((FLOOR_CODE >= FLOOR_CODE_SUBFLOOR_ONE) && (FLOOR_CODE <= FLOOR_CODE_SUBFLOOR_NINE))
    {
        (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s%s", S_SYMBOL_TABLE[SYMBOL_HYPHEN],
                        S_SYMBOL_TABLE[FLOOR_CODE - FLOOR_CODE_SUBFLOOR_ONE + 1]);

        p_ctx->actual_floor_number = FLOOR_CODE;
        return;
    }

    // Номера этажей от "0" до "40"
    if (FLOOR_CODE < 10)
    {
        (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s", S_SYMBOL_TABLE[FLOOR_CODE % 10]);
    }
    else
    {
        (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s%s", S_SYMBOL_TABLE[FLOOR_CODE / 10],
                        S_SYMBOL_TABLE[FLOOR_CODE % 10]);
    }

    p_ctx->actual_floor_number = FLOOR_CODE;
}

// Байт W[1]: код сообщения
static void decode_message_code(uim_ctx_t *p_ctx, const uint8_t *p_data)
{
    const uint8_t MESSAGE_CODE = p_data[3] & MESSAGE_CODE_MASK;
    // TODO: доделать
}

// Байт W[3]
static void decode_signal_code(uim_ctx_t *p_ctx, const uint8_t *p_data)
{
    const uint8_t ARROW_CODE = p_data[5] & ARROW_MASK;

    // Текущее направление движения
    switch (ARROW_CODE)
    {
    case 0x01:
        p_ctx->state.direction = SUL_DIR_DOWN;
        break;
    case 0x02:
        p_ctx->state.direction = SUL_DIR_UP;
        break;
    default:
        p_ctx->state.direction = SUL_DIR_NONE;
        break;
    }

    p_ctx->state.arrival  = ((p_data[5] & ARRIVAL_SIGNAL_BIT_MASK) == 1U);
    p_ctx->state.movement = ((p_data[5] & MOVEMENT_BIT_MASK) == 1U);
}

// Проверка бита необходимости ответа СУЛ
static bool is_response_needed(const uint8_t *p_data)
{
    return (p_data[0] & RESPONSE_NEEDED_BIT_MASK) == RESPONSE_NEEDED_BIT_MASK ? true : false;
}

sul_status_t uim_can_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out)
{
    uim_ctx_t *p_state = (uim_ctx_t *) p_ctx;

    /* Сдвиг ID по адресу станции (id пакетов адресно-зависим). */
    const uint32_t RX_ID = p_frame->id;

    /* ID известного пакета совпал, но DLC не тот — малформированный кадр. */
    if (p_frame->p_data[0] == 0x81 && p_frame->p_data[0] == 0x00)
    {
        if (p_frame->len != PROTO_DLC)
        {
            return SUL_STATUS_ERR;
        }

        if (RX_ID != p_state->uim_address)
        {
            return SUL_STATUS_IGNORED;
        }

        switch (p_state->uim_address)
        {
        case CABIN_INDICATOR_ID:
            p_state->cop_mode       = false;
            p_state->should_respond = is_response_needed(p_frame->p_data);
            break;
        case MAIN_FLOOR_INDICATOR_ID:
        case AUX_MAIN_FLOOR_INDICATOR_ID:
            p_state->cop_mode       = true;
            p_state->should_respond = is_response_needed(p_frame->p_data);
            break;
        case SECONDARY_CABIN_INDICATOR_ID:
            p_state->cop_mode       = false;
            p_state->should_respond = false;
            break;
        default:
            p_state->cop_mode       = true;
            p_state->should_respond = false;
        }
    }
    else
    {
        return SUL_STATUS_IGNORED;
    }

    decode_floor_code(p_state, p_frame->p_data);
    decode_message_code(p_state, p_frame->p_data);
    decode_signal_code(p_state, p_frame->p_data);

    if (p_state->should_respond)
    {
        (void) 0;
        //TODO: отправка отклика
    }

    *p_out = p_state->state;
    return SUL_STATUS_OK;
}
