#include "domain/sul/uim.h"

#include <stdbool.h>
#include <stdio.h>

#define PROTO_DLC 6U

/* Заголовок информационного кадра УИМ — data[0..1]. */
#define HEADER_BYTE_0 0x81U
#define HEADER_BYTE_1 0x00U

#define UIM_ADDRESS_MAX 50U /* адрес индикатора = CAN ID кадра (1..40 этажный, 46..50 роли) */

/* ── Гистерезис удержания спецрежима ────────────────────────────────────────
 *
 * Порт `special_mode_en_cnt` из OLD_PROJECT_TFT4_UIM (main_programm.c), но
 * ТОЛЬКО в той части, которая относится к ПРОТОКОЛУ, а не к отрисовке.
 *
 * ЗАЧЕМ НУЖЕН ЗДЕСЬ (протокольная причина, никуда не денется): код этажа и
 * код спецрежима приходят в ОДНОМ И ТОМ ЖЕ байте W_2 — станция ЧЕРЕДУЕТ кадры
 * (кадр с режимом, кадр с этажом, снова с режимом...). Наивный «сбросить флаги
 * в начале каждого кадра» гасил бы режим на каждом «этажном» кадре, т.е. флаг
 * режима дёргался бы с частотой шины. Декодер обязан ДЕРЖАТЬ режим между
 * кадрами, потому что протокол его не повторяет.
 *
 * Удержание асимметрично: режим взводится БЫСТРО (+6 одним кадром),
 * отпускается МЕДЛЕННО (−2 за кадр с обычным этажом) — нужно 3 «этажных»
 * кадра подряд, чтобы погасить режим, взведённый одним. Флаги режима
 * сбрасываются ровно в момент достижения нуля.
 *
 * ЧТО ИЗ ЭТАЛОНА СОЗНАТЕЛЬНО НЕ ПЕРЕНЕСЕНО (это была специфика ЕГО
 * презентации, не протокола):
 *   1. Заморозка направления. В эталоне весь `switch (arrow_code)` стоял под
 *      `if (special_mode_en_cnt == 0)`, потому что режим и стрелка делили ОДИН
 *      слот вывода (`icon_img_ptr`) — обновить стрелку значило стереть иконку
 *      режима. Здесь `direction` и режим — независимые поля sul_result_t, а
 *      layout (Фазы 4/5) выводит спрайт режима ОДНОВРЕМЕННО с этажом и
 *      стрелкой. Поэтому направление обновляется КАЖДЫМ кадром: замораживать
 *      его значило бы отдавать наверх устаревшую стрелку.
 *   2. Глушение звука (`is_playing_sounds_prohibited`). В новой архитектуре
 *      это забота audio_policy (Фаза 6) — она читает разрешённый режим из
 *      indication_task_t; декодеру про звук знать нечего.
 *
 * Потолок насыщения нужен, чтобы долго висящий режим не потребовал
 * CEILING/RELEASE кадров на выход (при 500/2 — максимум 250 кадров).
 */
#define SPECIAL_MODE_ATTACK  6U /* +6: кадр с кодом режима / перегрузом */
#define SPECIAL_MODE_RELEASE 2U /* −2: кадр с обычным кодом этажа       */
#define SPECIAL_MODE_CEILING 500U /* потолок насыщения (как в эталоне)    */

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

/* Отклик станции (эталон send_response_to_station): 2 байта `0x81 0x00` на
 * CAN ID «адрес индикатора + 0x80». */
#define RESPONSE_ID_OFFSET 0x80U
#define RESPONSE_DLC       2U

/* Таблица символов: у УИМ код этажа — обычное десятичное число, поэтому
 * таблица используется только как источник цифр и дефиса (эталон печатает их
 * через sprintf напрямую). Общий вид сохранён под будущие буквенные коды. */
static const char *const S_SYMBOL_TABLE[SYMBOL_TOTAL] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "b", "C", "d", "E", "F", " ", "П", "Р",
    "п", "H", "U", "-", "_", "u", "L", "У", "Б", "Г", "R", "V", "N", "S", "K", "Y", "G", "B", "T",
};

void uim_init(uim_ctx_t *p_ctx)
{
    p_ctx->state = sul_default_state();

    p_ctx->actual_floor_number = 0U;
    p_ctx->uim_address = CABIN_INDICATOR_ID; /* По умолчанию: индикатор в кабине */
    p_ctx->cop_mode         = false;
    p_ctx->should_respond   = false;
    p_ctx->special_mode_cnt = 0U;
}

void uim_set_address(uim_ctx_t *p_ctx, uint8_t uim_address)
{
    p_ctx->uim_address = (uim_address <= UIM_ADDRESS_MAX) ? uim_address : UIM_ADDRESS_MAX;
}

/* ── Гистерезис: attack / release / сброс флагов ──────────────────────────── */

/**
 * @brief Сбросить ВСЕ флаги спецрежимов разом.
 *
 * Единственная точка гашения режима — вызывается ровно в момент, когда
 * счётчик гистерезиса достигает нуля. В эталоне ту же роль играет
 * перезапись `icon_img_ptr` стрелкой/NULL внутри `if (cnt == 0)`.
 */
static void clear_special_modes(uim_ctx_t *p_ctx)
{
    p_ctx->state.overload    = false;
    p_ctx->state.fire_alarm  = false;
    p_ctx->state.maintenance = false;
    p_ctx->state.fireman     = false;
    p_ctx->state.evacuation  = false;
    p_ctx->state.lading      = false;
    p_ctx->state.seismic     = false;
    p_ctx->state.error       = false;
}

/** Взвести/продлить удержание спецрежима (насыщение на потолке). */
static void special_mode_attack(uim_ctx_t *p_ctx)
{
    const uint16_t NEXT = (uint16_t) (p_ctx->special_mode_cnt + SPECIAL_MODE_ATTACK);

    p_ctx->special_mode_cnt = (NEXT > SPECIAL_MODE_CEILING) ? SPECIAL_MODE_CEILING : NEXT;
}

/**
 * @brief Ослабить удержание на один «этажный» кадр; на нуле — погасить режимы.
 *
 * Вычитание с насыщением в нуль (эталонное `if (cnt > 0) cnt -= 2;` при
 * нечётном счётчике ушло бы в underflow uint16 — здесь это невозможно).
 */
static void special_mode_release(uim_ctx_t *p_ctx)
{
    if (p_ctx->special_mode_cnt == 0U)
    {
        return; /* уже обычная индикация — гасить нечего */
    }

    p_ctx->special_mode_cnt = (p_ctx->special_mode_cnt <= SPECIAL_MODE_RELEASE)
                                  ? 0U
                                  : (uint16_t) (p_ctx->special_mode_cnt - SPECIAL_MODE_RELEASE);

    if (p_ctx->special_mode_cnt == 0U)
    {
        clear_special_modes(p_ctx);
    }
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

/**
 * @brief Байт W[2] — код этажа ЛИБО код спецрежима (различаются диапазоном).
 *
 * Диапазоны (эталон main_programm.c, ветки по floor_code):
 *   0..40  — обычный этаж          → RELEASE гистерезиса + обновление pos
 *   41..49 — подземный «-1»..«-9»  → RELEASE гистерезиса + обновление pos
 *   50     — резерв                → НЕ трогаем ничего (в эталоне тоже нет ветки)
 *   51..59 — спецрежим             → ATTACK гистерезиса + ровно один флаг
 *   60..63 — вне протокола         → НЕ трогаем ничего
 *
 * Флаги спецрежимов, которыми владеет ЭТОТ байт (все, кроме `overload` —
 * тот приходит кодом сообщения, см. decode_message_code()), выставляются по
 * принципу «последний победил»: в эталоне спецрежим — единственный
 * `icon_img_ptr`, одновременно двух не бывает. Сброс — не здесь, а в
 * clear_special_modes() по истечении гистерезиса (единственная точка гашения).
 */
static void decode_floor_code(uim_ctx_t *p_ctx, const uint8_t *p_data)
{
    const uint8_t FLOOR_CODE = p_data[4] & FLOOR_MASK;

    /* ── Спецрежимы 51..59 ── */
    if ((FLOOR_CODE >= FLOOR_CODE_SEISMIC_HAZARD) && (FLOOR_CODE <= FLOOR_CODE_LADING))
    {
        /* ATTACK всегда, даже для кода без поля в модели (иначе кадр
         * распознанного режима работал бы как «обычный этаж» и ОТПУСКАЛ
         * удержание — индикация замигала бы). */
        special_mode_attack(p_ctx);

        /* Владелец группы — этот байт: гасим свои флаги и ставим один. */
        p_ctx->state.fire_alarm  = false;
        p_ctx->state.maintenance = false;
        p_ctx->state.fireman     = false;
        p_ctx->state.evacuation  = false;
        p_ctx->state.lading      = false;
        p_ctx->state.seismic     = false;
        p_ctx->state.error       = false;

        switch (FLOOR_CODE)
        {
        case FLOOR_CODE_SEISMIC_HAZARD:
            p_ctx->state.seismic = true;
            break;
        case FLOOR_CODE_FIREMAN:
            p_ctx->state.fireman = true;
            break;
        case FLOOR_CODE_MAINTENANCE:
            p_ctx->state.maintenance = true;
            break;
        case FLOOR_CODE_EVACUATION:
            p_ctx->state.evacuation = true;
            break;
        case FLOOR_CODE_FIRE_ALARM:
            p_ctx->state.fire_alarm = true;
            break;
        case FLOOR_CODE_LADING:
            p_ctx->state.lading = true;
            break;
        case FLOOR_CODE_FAILURE:
        case FLOOR_CODE_OUT_OF_SERVICE_1:
        case FLOOR_CODE_OUT_OF_SERVICE_2:
            p_ctx->state.error = true;
            break;
        default:
            /* Код из диапазона 51..59 без назначения в этой версии протокола:
             * режим УДЕРЖИВАЕТСЯ гистерезисом (индикация не мигает), но флага
             * нет → mode_priority отдаст NORMAL. */
            break;
        }
        return;
    }

    /* ── Резерв (50) и коды вне протокола (60..63) — состояние не меняем ── */
    if (FLOOR_CODE > FLOOR_CODE_SUBFLOOR_NINE)
    {
        return;
    }

    /* ── Обычный этаж: ослабляем удержание спецрежима ── */
    special_mode_release(p_ctx);

    // Номера этажей от "-1" до "-9"
    if (FLOOR_CODE >= FLOOR_CODE_SUBFLOOR_ONE)
    {
        (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s%s", S_SYMBOL_TABLE[SYMBOL_HYPHEN],
                        S_SYMBOL_TABLE[FLOOR_CODE - FLOOR_CODE_SUBFLOOR_ONE + 1]);

        p_ctx->actual_floor_number = FLOOR_CODE;
        return;
    }

    // Номера этажей от "0" до "40"
    if (FLOOR_CODE < 10)
    {
        (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s", S_SYMBOL_TABLE[FLOOR_CODE]);
    }
    else
    {
        (void) snprintf(p_ctx->state.pos, SUL_POS_BUF_LEN, "%s%s", S_SYMBOL_TABLE[FLOOR_CODE / 10],
                        S_SYMBOL_TABLE[FLOOR_CODE % 10]);
    }

    p_ctx->actual_floor_number = FLOOR_CODE;
}

/**
 * @brief Байт W[1] — код сообщения (озвучка/события).
 *
 * Из всей таблицы на индикацию влияет только «Перегруз»: в эталоне он
 * взводит тот же гистерезис (+6) и рисует свою иконку. Владелец флага
 * `overload` — ЭТОТ байт (сброс — общий, по истечении гистерезиса).
 *
 * Остальные коды (движение вверх/вниз, двери, кнопка, неисправность) —
 * звуковые события: TODO Фаза 6 (audio_policy), на sul_result_t не влияют.
 */
static void decode_message_code(uim_ctx_t *p_ctx, const uint8_t *p_data)
{
    const uint8_t MESSAGE_CODE = p_data[3] & MESSAGE_CODE_MASK;

    if (MESSAGE_CODE == MSG_CODE_VOICE_OVERLOAD)
    {
        special_mode_attack(p_ctx);
        p_ctx->state.overload = true;
    }
}

/**
 * @brief Байт W[3] — направление, гонг, движение.
 *
 * Все три поля обновляются БЕЗУСЛОВНО, включая направление под удерживаемым
 * спецрежимом: режим и стрелка — независимые поля модели, layout выводит их
 * одновременно (см. блок про гистерезис выше, п.1 «не перенесено»).
 */
static void decode_signal_code(uim_ctx_t *p_ctx, const uint8_t *p_data)
{
    p_ctx->state.arrival  = ((p_data[5] & ARRIVAL_SIGNAL_BIT_MASK) == ARRIVAL_SIGNAL_BIT_MASK);
    p_ctx->state.movement = ((p_data[5] & MOVEMENT_BIT_MASK) == MOVEMENT_BIT_MASK);

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
    case 0x03:
        p_ctx->state.direction = SUL_DIR_DOUBLE;
        break;
    default:
        p_ctx->state.direction = SUL_DIR_NONE;
        break;
    }
}

/**
 * @brief Бит «нужен отклик СУЛ» — W_3 (data[5]), бит 7, АКТИВЕН НУЛЁМ.
 *
 * Эталон (main_programm.c:215): `(((msg.W_3 & 0x80) >> 7U) == 0) ? true : false`
 * — отклик нужен, когда бит СБРОШЕН. Инверсия неочевидна, поэтому явно.
 */
static bool is_response_needed(const uint8_t *p_data)
{
    return (p_data[5] & RESPONSE_NEEDED_BIT_MASK) == 0U;
}

sul_status_t uim_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out)
{
    uim_ctx_t *p_state = (uim_ctx_t *) p_ctx;

    /* Запрос на отклик ТРАНЗИТЕН — гасим в начале КАЖДОГО вызова (контракт
     * sul_take_pending_tx_fn_t): иначе кадр, отброшенный любой проверкой
     * ниже, оставил бы висеть отклик от предыдущего кадра, и app-слой уехал
     * бы с ним в шину повторно. */
    p_state->should_respond = false;

    /* У УИМ CAN ID кадра РАВЕН адресу индикатора (не база+сдвиг, как у
     * НКУ-CAN) — HW-фильтр пропускает ровно наш ID, эта проверка страхует
     * host-путь и возможную рассинхронизацию фильтра с настройкой. */
    if (p_frame->id != p_state->uim_address)
    {
        return SUL_STATUS_IGNORED;
    }

    /* ID наш, но длина не протокольная — малформированный кадр (порядок
     * проверок — по контракту sul.h: сначала «наш ли», потом «целый ли»).
     * Заодно гарантирует, что p_data[0..5] читать безопасно. */
    if (p_frame->len != PROTO_DLC)
    {
        return SUL_STATUS_ERR;
    }

    /* Не информационный кадр (другой класс сообщения от той же станции) —
     * не ошибка, просто не наш случай. */
    if ((p_frame->p_data[0] != HEADER_BYTE_0) || (p_frame->p_data[1] != HEADER_BYTE_1))
    {
        return SUL_STATUS_IGNORED;
    }

    /* Роль индикатора и необходимость отклика — по адресу (он же ID). */
    switch (p_state->uim_address)
    {
    case CABIN_INDICATOR_ID:
        p_state->cop_mode       = true;
        p_state->should_respond = is_response_needed(p_frame->p_data);
        break;
    case MAIN_FLOOR_INDICATOR_ID:
    case AUX_MAIN_FLOOR_INDICATOR_ID:
        p_state->cop_mode       = false;
        p_state->should_respond = is_response_needed(p_frame->p_data);
        break;
    case SECONDARY_CABIN_INDICATOR_ID:
        p_state->cop_mode = true;
        break;
    default:
        p_state->cop_mode = false;
        break;
    }
    /* should_respond для прочих ролей остаётся false — сброшен выше. */

    /* Порядок как в эталоне и он ЗНАЧИМ для гистерезиса: код сообщения
     * (ATTACK перегруза) → код этажа (ATTACK режима ЛИБО RELEASE) → сигналы.
     * Перегруз в кадре с обычным этажом даёт +6−2 = +4, т.е. режим держится —
     * как в эталоне. decode_signal_code() от счётчика уже не зависит, но
     * порядок сохранён: он описывает раскладку кадра W_1→W_2→W_3. */
    decode_message_code(p_state, p_frame->p_data);
    decode_floor_code(p_state, p_frame->p_data);
    decode_signal_code(p_state, p_frame->p_data);

    /* Отклик (should_respond) НЕ отправляется здесь: домену шина недоступна
     * (ARCH §4). Его забирает app-слой через uim_take_pending_tx() ниже. */

    *p_out = p_state->state;
    return SUL_STATUS_OK;
}

bool uim_take_pending_tx(void *p_ctx, sul_tx_frame_t *p_out)
{
    uim_ctx_t *p_state = (uim_ctx_t *) p_ctx;

    if (!p_state->should_respond)
    {
        return false;
    }

    /* Одноразово: отклик на КАЖДЫЙ требующий его кадр ровно один. Гасим сразу,
     * не дожидаясь следующего decode() — app-слой может вызвать нас повторно. */
    p_state->should_respond = false;

    p_out->id      = (uint32_t) p_state->uim_address + RESPONSE_ID_OFFSET;
    p_out->len     = RESPONSE_DLC;
    p_out->data[0] = HEADER_BYTE_0;
    p_out->data[1] = HEADER_BYTE_1;

    return true;
}
