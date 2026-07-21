#include "domain/sul/nku_can.h"

#include <stdbool.h>
#include <stdio.h>

/* Базовые ID (для адреса станции 0). Реальный ID = base | сдвиг группы адреса:
 * PACKET1..4 — group4 = nku_address<<4 (биты [7:4]); PACKET5 — group6 =
 * nku_address<<6 (биты [8:6]). Адрес приходит из настроек (Фаза 3.1,
 * proto_slice[0]); nku_can_set_address(). Для адреса 0 сдвиг нулевой — поведение
 * идентично Фазам 1/2. */
#define PACKET1_BASE 0x506U /* направление, режимы, начало движения, двери */
#define PACKET2_BASE 0x408U /* перегруз (вариант 1)                        */
#define PACKET3_BASE 0x508U /* позиция кабины, гонг, временная погрузка    */
#define PACKET4_BASE 0x50BU /* перегруз (вариант 2), сейсмоопасность       */
#define PACKET5_BASE 0x606U /* следующий этаж                             */
#define PROTO_DLC    8U

#define NKU_ADDRESS_MAX 15U /* адрес 0..15; group4 = addr<<4 */

#define ARROW_MASK     0x03U /* PACKET1 data[6][1:0] — стрелка                */
#define MOVEMENT_MASK  0x0CU /* PACKET1 data[6][3:2] — начало движения        */
#define ICON_MASK      0xF0U /* PACKET1 data[6][7:4] — код режима             */
#define FLOOR_MASK 0x3FU /* символ этажа / числовой уровень                   */
/* Двери (PACKET1 data[4]: откр. 0x10 / закр. 0x20) — только озвучка, не поле
 * §6; декодируются в Фазе 6 (audio_policy). Здесь намеренно не разбираются. */

/* Коды режима в нибле data[6] & ICON_MASK (PACKET1). Точные значения нибла. */
#define ICON_LADING   0x10U /* инструментальная погрузка */
#define ICON_MP1      0x30U /* сервис / МП1              */
#define ICON_REVISION 0x40U /* ревизия                  */
#define ICON_MP2      0x50U /* сервис / МП2              */
#define ICON_FIRE     0x70U /* пожарная тревога         */
#define ICON_FIREMAN  0xF0U /* режим пожарного          */

#define WEIGHT_MASK     0x40U /* PACKET2 data[7] / PACKET4 data[5] — перегруз   */
#define GONG_MASK       0x40U /* PACKET3 data[3] — гонг активен при БИТЕ == 0   */
#define LADING_SEC_MASK 0x3FU /* PACKET3 data[2] — секунды погрузки           */
#define LADING_MIN_MASK 0x0FU /* PACKET3 data[3] — минуты погрузки            */
#define SEISMIC_MASK    0x80U /* PACKET4 data[0] — сейсмоопасность              */

#define SYMBOL_SPACE  16U
#define SYMBOL_A      10U
#define SYMBOL_P      17U /* "П" */
#define SYMBOL_p      19U /* "п" */
#define SYMBOL_HYPHEN 22U /* "-" */
#define SYMBOL_TOTAL  38U

#define FLOOR_NUM_UNKNOWN 60U /* «н/д» для озвучки — как в legacy floor_number_parser */

/* Таблица символов НКУ-CAN — порт из OLD_PROJECT floor_string_composer()
 * (source/main_programm.c). Индекс — код символа с шины (байт & FLOOR_MASK). */
static const char *const S_SYMBOL_TABLE[SYMBOL_TOTAL] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "b", "C", "d", "E", "F", " ", "П", "Р",
    "п", "H", "U", "-", "_", "u", "L", "У", "Б", "Г", "R", "V", "N", "S", "K", "Y", "G", "B", "T",
};

void nku_can_init(nku_can_ctx_t *p_ctx)
{
    p_ctx->state         = sul_default_state();
    p_ctx->overload_p2   = false;
    p_ctx->overload_p4   = false;
    p_ctx->lading_instr  = false;
    p_ctx->current_level = 0U;
    p_ctx->nku_address   = 0U; /* Фаза 3.1: caller задаёт из настроек через set_address() */
}

void nku_can_set_address(nku_can_ctx_t *p_ctx, uint8_t nku_address)
{
    p_ctx->nku_address = (nku_address <= NKU_ADDRESS_MAX) ? nku_address : NKU_ADDRESS_MAX;
}

/* Пересчёт выходных полей, кормящихся несколькими пакетами (см. nku_can.h). */
static void recompute_multi_source(nku_can_ctx_t *p_ctx)
{
    p_ctx->state.overload = p_ctx->overload_p2 || p_ctx->overload_p4;
    p_ctx->state.lading   = p_ctx->lading_instr || (p_ctx->state.lading_secs > 0U);
}

/**
 * @brief Составить UTF-8 строку этажа из кодов левого/правого символа.
 *
 * Порт floor_string_composer(): left==SPACE или left==0 → однозначный этаж
 * (только правый символ; станция шлёт 0x00 как «нет левого символа», трактуем
 * как пробел). Возврат false — код вне таблицы (малформированный кадр).
 */
static bool compose_chars(uint8_t left, uint8_t right, char *p_out)
{
    if ((left >= SYMBOL_TOTAL) || (right >= SYMBOL_TOTAL))
    {
        return false;
    }

    if ((left == SYMBOL_SPACE) || (left == 0U))
    {
        (void) snprintf(p_out, SUL_POS_BUF_LEN, "%s", S_SYMBOL_TABLE[right]);
    }
    else
    {
        (void) snprintf(p_out, SUL_POS_BUF_LEN, "%s%s", S_SYMBOL_TABLE[left],
                        S_SYMBOL_TABLE[right]);
    }

    return true;
}

/**
 * @brief Производный числовой этаж для озвучки — порт floor_number_parser().
 *
 * Стандартные (0..40): left*10+right (или только right при пробеле).
 * Отрицательные ("-" слева): 40+right. Подвальные ("П"/"п" слева): 50+right.
 * Всё нераспознанное / вне диапазона → FLOOR_NUM_UNKNOWN (60).
 */
static uint8_t floor_number_parser(uint8_t left, uint8_t right)
{
    if (((left < SYMBOL_A) || (left == SYMBOL_SPACE)) && (right < SYMBOL_A))
    {
        const uint8_t FLOOR = (left == SYMBOL_SPACE) ? right : (uint8_t) (left * 10U + right);
        return (FLOOR < 41U) ? FLOOR : FLOOR_NUM_UNKNOWN;
    }

    if ((left == SYMBOL_HYPHEN) && (right < SYMBOL_A))
    {
        return (right > 0U) ? (uint8_t) (40U + right) : FLOOR_NUM_UNKNOWN;
    }

    if (((left == SYMBOL_P) || (left == SYMBOL_p)) && (right < SYMBOL_A))
    {
        return (uint8_t) (50U + right);
    }

    return FLOOR_NUM_UNKNOWN;
}

/**
 * @brief PACKET1 (0x506) — направление, режим, начало движения, уровень остановки.
 *
 * data[6]: [1:0] стрелка · [3:2] начало движения · [7:4] код режима.
 * data[3][5:0]: числовой уровень остановки (гейт «следующего этажа», PACKET5).
 *
 * Режимные флаги, которыми владеет ТОЛЬКО PACKET1 (fire/maintenance/fireman и
 * инструментальная погрузка), сбрасываются в начале и выставляются по нибле —
 * так пакет-без-режима гасит устаревший режим (как в legacy). overload и
 * seismic PACKET1 НЕ трогает (ими владеют PACKET2/4).
 */
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

/**
 * @brief PACKET3 (0x508) — позиция кабины, гонг, временная погрузка.
 *
 * @return false — код символа позиции вне таблицы (малформированный кадр).
 */
static bool decode_packet3(nku_can_ctx_t *p_ctx, const uint8_t *p_data)
{
    const uint8_t LEFT  = p_data[5] & FLOOR_MASK;
    const uint8_t RIGHT = p_data[6] & FLOOR_MASK;

    if (!compose_chars(LEFT, RIGHT, p_ctx->state.pos))
    {
        return false;
    }
    p_ctx->state.floor_num = floor_number_parser(LEFT, RIGHT);

    /* Гонг активен, когда БИТ 0x40 в data[3] СБРОШЕН (инверсная кодировка). */
    p_ctx->state.arrival = ((p_data[3] & GONG_MASK) == 0U);

    /* Временная погрузка: остаток = минуты*60 + секунды. */
    const uint8_t SECS       = p_data[2] & LADING_SEC_MASK;
    const uint8_t MINS       = p_data[3] & LADING_MIN_MASK;
    p_ctx->state.lading_secs = (uint16_t) ((uint16_t) MINS * 60U + SECS);

    return true;
}

/**
 * @brief PACKET5 (0x606) — следующий этаж.
 *
 * Доверяем «следующему этажу» только пока кабина реально едет (↑/↓) и байт
 * назначения (data[0]) отличается от текущего уровня остановки — 0x606 и 0x506
 * приходят асинхронно, устаревший байт назначения на стоянке порождал бы
 * ложную индикацию (боевая заметка legacy). Иначе — очищаем next.
 *
 * @return false — код символа вне таблицы (малформированный кадр); при этом
 *         поле next не трогаем.
 */
static bool decode_packet5(nku_can_ctx_t *p_ctx, const uint8_t *p_data)
{
    const bool MOVING =
        (p_ctx->state.direction == SUL_DIR_UP) || (p_ctx->state.direction == SUL_DIR_DOWN);

    if (MOVING && (p_data[0] != p_ctx->current_level))
    {
        return compose_chars(p_data[3] & FLOOR_MASK, p_data[4] & FLOOR_MASK, p_ctx->state.next);
    }

    p_ctx->state.next[0] = '\0';
    return true;
}

sul_status_t nku_can_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out)
{
    nku_can_ctx_t *p_state = (nku_can_ctx_t *) p_ctx;

    /* Сдвиг ID по адресу станции (id пакетов адресно-зависим). */
    const uint32_t G4 = (uint32_t) p_state->nku_address << 4U;
    const uint32_t G6 = (uint32_t) p_state->nku_address << 6U;
    const uint32_t ID = p_frame->id;

    /* ID известного пакета совпал, но DLC не тот — малформированный кадр. */
    if ((ID == (PACKET1_BASE | G4)) || (ID == (PACKET2_BASE | G4)) ||
        (ID == (PACKET3_BASE | G4)) || (ID == (PACKET4_BASE | G4)) ||
        (ID == (PACKET5_BASE | G6)))
    {
        if (p_frame->len != PROTO_DLC)
        {
            return SUL_STATUS_ERR;
        }
    }

    if (ID == (PACKET1_BASE | G4))
    {
        decode_packet1(p_state, p_frame->p_data);
    }
    else if (ID == (PACKET2_BASE | G4))
    {
        p_state->overload_p2 = ((p_frame->p_data[7] & WEIGHT_MASK) == WEIGHT_MASK);
    }
    else if (ID == (PACKET3_BASE | G4))
    {
        if (!decode_packet3(p_state, p_frame->p_data))
        {
            return SUL_STATUS_ERR;
        }
    }
    else if (ID == (PACKET4_BASE | G4))
    {
        p_state->overload_p4   = ((p_frame->p_data[5] & WEIGHT_MASK) == WEIGHT_MASK);
        p_state->state.seismic = ((p_frame->p_data[0] & SEISMIC_MASK) == SEISMIC_MASK);
    }
    else if (ID == (PACKET5_BASE | G6))
    {
        if (!decode_packet5(p_state, p_frame->p_data))
        {
            return SUL_STATUS_ERR;
        }
    }
    else
    {
        return SUL_STATUS_IGNORED;
    }

    recompute_multi_source(p_state);
    *p_out = p_state->state;
    return SUL_STATUS_OK;
}
