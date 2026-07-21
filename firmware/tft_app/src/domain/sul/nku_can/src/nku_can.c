#include "domain/sul/nku_can.h"

#include <stdbool.h>
#include <stdio.h>

/* Фаза 1: адрес станции захардкожен в 0 — базовые ID без сдвига группы
 * (легаси: 0x506|group4, group4 = nku_address<<4; для address=0 group4=0).
 * Фаза 3 параметризует через настройки. */
#define PACKET1_ID 0x506U
#define PACKET3_ID 0x508U
#define PROTO_DLC  8U

#define ARROW_MASK 0x03U
#define FLOOR_MASK 0x3FU

#define SYMBOL_SPACE 16U
#define SYMBOL_TOTAL 38U

/* Таблица символов НКУ-CAN — порт из OLD_PROJECT floor_string_composer()
 * (source/main_programm.c). Индекс — код символа с шины (байт & FLOOR_MASK). */
static const char *const S_SYMBOL_TABLE[SYMBOL_TOTAL] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "b", "C", "d", "E", "F", " ", "П", "Р",
    "п", "H", "U", "-", "_", "u", "L", "У", "Б", "Г", "R", "V", "N", "S", "K", "Y", "G", "B", "T",
};

void nku_can_init(nku_can_ctx_t *p_ctx)
{
    p_ctx->state = sul_default_state();
}

/**
 * @brief PACKET1 (0x506) — направление движения.
 *
 * ARROW_MASK=0x03 на data[6] (DATA7): 0=нет/1=вверх/2=вниз/3=двойная стрелка.
 * Легаси (msg_receiver_task, PACKET1) обрабатывал только 0/1/2 — case 3
 * отсутствовал (направление молча не менялось). 3 = «двойная стрелка»
 * (спецрежим индикации, словарь special/DisplayArrowIcon) — уточнено отдельно.
 *
 * Маска покрывает ровно 0..3 — весь диапазон sul_direction_t, прямое
 * приведение типа корректно без switch/default.
 */
static void decode_packet1(nku_can_ctx_t *p_ctx, const uint8_t *p_data)
{
    p_ctx->state.direction = (sul_direction_t) (p_data[6] & ARROW_MASK);
}

/**
 * @brief PACKET3 (0x508) — позиция кабины (left/right символы).
 *
 * FLOOR_MASK=0x3F на data[5] (left) и data[6] (right) — порт
 * floor_string_composer() из OLD_PROJECT. left==SPACE или left==0 →
 * однозначный этаж, выводится только правый символ (легаси трактует байт
 * 0x00 так же, как пробел — станция может слать нулевой байт вместо явного
 * кода пробела для «нет левого символа»; порт без изменений поведения).
 *
 * В отличие от легаси (молча оставляет буфер как есть при выходе за
 * SYMBOL_TOTAL — маска даёт до 63 сырых значений при 38 валидных символах),
 * здесь это ошибка (false) — не полагаемся на предыдущее содержимое буфера.
 *
 * @return false — left/right вне таблицы символов (малформированный кадр).
 */
static bool decode_packet3(nku_can_ctx_t *p_ctx, const uint8_t *p_data)
{
    const uint8_t LEFT  = p_data[5] & FLOOR_MASK;
    const uint8_t RIGHT = p_data[6] & FLOOR_MASK;

    if ((LEFT >= SYMBOL_TOTAL) || (RIGHT >= SYMBOL_TOTAL))
    {
        return false;
    }

    if ((LEFT == SYMBOL_SPACE) || (LEFT == 0U))
    {
        (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s", S_SYMBOL_TABLE[RIGHT]);
    }
    else
    {
        (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s%s", S_SYMBOL_TABLE[LEFT],
                        S_SYMBOL_TABLE[RIGHT]);
    }

    return true;
}

sul_status_t nku_can_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out)
{
    nku_can_ctx_t *p_state = (nku_can_ctx_t *) p_ctx;

    if (p_frame->id == PACKET1_ID)
    {
        if (p_frame->len != PROTO_DLC)
        {
            return SUL_STATUS_ERR;
        }
        decode_packet1(p_state, p_frame->p_data);
        *p_out = p_state->state;
        return SUL_STATUS_OK;
    }

    if (p_frame->id == PACKET3_ID)
    {
        if (p_frame->len != PROTO_DLC)
        {
            return SUL_STATUS_ERR;
        }
        if (!decode_packet3(p_state, p_frame->p_data))
        {
            return SUL_STATUS_ERR;
        }
        *p_out = p_state->state;
        return SUL_STATUS_OK;
    }

    return SUL_STATUS_IGNORED;
}
