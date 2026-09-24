#include "ui/fallback.h"

#include "domain/mode_priority.h"
#include "services/gfx.h"
#include "ui/fallback_sprites.h"

#include "fallback_sprite_table.h"

#include <stdio.h>

/* Фаза 1/2/4.1: позиции захардкожены под TFT8 (800×600, панель текущего
 * стенда). Фаза 5 (layout-движок) заменит это таблицей виджетов из стиля.
 *
 * РАСКЛАДКА — ГОРИЗОНТАЛЬНАЯ ПОЛОСА ИЗ ТРЁХ СЛОТОВ:
 *
 *   ┌─────────┐                        ┌─────────┐
 *   │ стрелка │      1 2  (176 px)     │  режим  │
 *   │   ИЛИ   │                        │   ИЛИ   │
 *   │ отсчёт  │                        │диспетчер│
 *   └─────────┘                        └─────────┘
 *    200×200            центр               200×200
 *
 * Два решения, принятые пользователем и определившие именно такую форму:
 *
 * 1. «Этаж + значок ВМЕСТЕ». До 4.1 метка режима ЗАМЕЩАЛА номер этажа — это
 *    было упрощением safe-mode из-за единственной текстовой строки, а не
 *    правилом домена ([MODE_PRIORITY.md] это прямо оговаривает). При перегрузе
 *    или сервисе пассажир обязан продолжать видеть, где кабина.
 *
 * 2. Спрайты — 200×200 (минимум читаемости для TFT8). Отсюда полоса, а не
 *    стопка: 176 (этаж) + 200 + 200 = 576 плюс зазоры в 600 px по высоте уже
 *    не укладывается, а по ширине 200 + 264 + 200 = 664 из 800 — свободно.
 *
 * 3. Диспетчерский вход НЕ полноэкранный: он занимает слот режима (и тем
 *    перекрывает режим СУЛ, как требует §3.4), но этаж и стрелку не трогает.
 *    Это осознанная ревизия формулировки §3.4 «перекрывает и обычную
 *    индикацию»: перекрывать номер этажа нельзя.
 *
 * Слоты заданы ЦЕНТРАМИ, а не углами: спрайт центрируется по своему
 * фактическому размеру, поэтому отклонение от номинала 200×200 не требует
 * правки кода (в ТЗ на исходники размер задан рекомендацией с допуском). */
#define PANEL_WIDTH  800U
#define PANEL_HEIGHT 600U

#define BAND_CENTER_Y  290U /* общая полоса — центр по вертикали             */
#define ARROW_CENTER_X 120U /* левый слот: стрелка ЛИБО отсчёт погрузки      */
#define FLOOR_CENTER_X 400U /* центр экрана — номер этажа                    */
#define MODE_CENTER_X  680U /* правый слот: режим ЛИБО диспетчер             */

/* Глиф FloorFontFallback — 176 px высотой (проверено по данным шрифта).
 * Держим отдельной константой: gfx не отдаёт высоту строки, а сажать текст
 * «на глаз» относительно спрайтов нельзя — полоса перестанет быть полосой. */
#define FLOOR_GLYPH_H 176U
#define FLOOR_Y       (BAND_CENTER_Y - (FLOOR_GLYPH_H / 2U))

#define ARROW_PRIMITIVE_SIZE 160U /* ПРИМИТИВНАЯ стрелка — только когда спрайта нет */

/**
 * @brief Короткая текстовая метка спецрежима.
 *
 * Со Фазы 4.1 это НЕ основной способ показа режима, а ДЕГРАДАЦИЯ: рисуется
 * только когда спрайта режима в образе нет (ARCH §11 — по-виджетно, не полный
 * откат экрана). Согласовано: при наличии спрайта текст не дублируется.
 */
static const char *mode_label(sul_mode_t mode)
{
    switch (mode)
    {
        case SUL_MODE_FIREMAN:     return "ПОЖАРНЫЙ";
        case SUL_MODE_FIRE_ALARM:  return "ПОЖАР";
        case SUL_MODE_EVACUATION:  return "ЭВАКУАЦИЯ";
        case SUL_MODE_ERROR:       return "АВАРИЯ";
        case SUL_MODE_OVERLOAD:    return "ПЕРЕГРУЗ";
        case SUL_MODE_SEISMIC:     return "СЕЙСМО";
        case SUL_MODE_MAINTENANCE: return "СЕРВИС";
        case SUL_MODE_LADING:      return "ПОГРУЗКА";
        case SUL_MODE_NORMAL:
        default:                   return NULL;
    }
}

/**
 * @brief Метка диспетчерского входа (§3.4) — та же деградация, что у режимов.
 */
static const char *dispatcher_label(dispatcher_indication_t dispatcher)
{
    switch (dispatcher)
    {
        case DISPATCHER_INDICATION_TALKING: return "ОТВЕТ";
        case DISPATCHER_INDICATION_CALLING: return "ВЫЗОВ";
        case DISPATCHER_INDICATION_NONE:
        default:                            return NULL;
    }
}

/** Текст, отцентрованный по горизонтали относительно @p center_x. */
static void draw_text_at(const tFont *p_font, const char *p_str, uint16_t center_x, uint16_t y)
{
    const uint16_t WIDTH = gfx_string_width(p_font, p_str);
    const uint16_t X     = (WIDTH < center_x) ? (uint16_t) (center_x - (WIDTH / 2U)) : 0U;
    (void) gfx_draw_string(p_font, p_str, X, y, GFX_COLOR_WHITE);
}

/**
 * @brief Нарисовать спрайт по центру слота (@p center_x, @p center_y).
 *
 * Центрирование по ФАКТИЧЕСКОМУ размеру картинки, а не по номиналу слота:
 * размеры в ТЗ на исходники заданы рекомендацией с допуском, и отклонение не
 * должно требовать правки кода. Картинка шире/выше слота вылезет за его
 * границы, но не за экран — `gfx` обрезает.
 *
 * @return false — спрайта в образе нет, вызывающий обязан деградировать.
 */
static bool draw_sprite_at(fallback_sprite_id_t id, uint16_t center_x, uint16_t center_y)
{
    const fallback_sprite_entry_t *p_entry = fallback_sprite_entry(id);
    if (p_entry == NULL)
    {
        return false;
    }

    const uint16_t W = p_entry->p_image->width;
    const uint16_t H = p_entry->p_image->height;
    const uint16_t X = (W < center_x) ? (uint16_t) (center_x - (W / 2U)) : 0U;
    const uint16_t Y = (H < center_y) ? (uint16_t) (center_y - (H / 2U)) : 0U;

    if (p_entry->tinted)
    {
        gfx_draw_image_tinted(p_entry->p_image, p_entry->format, X, Y, p_entry->color);
    }
    else
    {
        gfx_draw_image(p_entry->p_image, p_entry->format, X, Y);
    }
    return true;
}

/** Левый слот: направление движения. */
static void render_direction(sul_direction_t direction)
{
    const fallback_sprite_id_t ID = fallback_sprite_for_direction(direction);
    if (ID == FALLBACK_SPRITE_NONE)
    {
        return; /* стоим — рисовать нечего */
    }

    if (draw_sprite_at(ID, ARROW_CENTER_X, BAND_CENTER_Y))
    {
        return;
    }

    /* Деградация: примитивная треугольная стрелка. Двойная стрелка примитивом
     * не выражается — в этом случае слот просто пуст, как было до 4.1. */
    if ((direction == SUL_DIR_UP) || (direction == SUL_DIR_DOWN))
    {
        const gfx_arrow_dir_t DIR = (direction == SUL_DIR_UP) ? GFX_ARROW_UP : GFX_ARROW_DOWN;
        const uint16_t X = (uint16_t) (ARROW_CENTER_X - (ARROW_PRIMITIVE_SIZE / 2U));
        const uint16_t Y = (uint16_t) (BAND_CENTER_Y - (ARROW_PRIMITIVE_SIZE / 2U));
        gfx_draw_arrow(DIR, X, Y, ARROW_PRIMITIVE_SIZE, GFX_COLOR_WHITE);
    }
}

/**
 * @brief Правый слот: диспетчерский вход, иначе режим СУЛ.
 *
 * Диспетчер проверяется ПЕРВЫМ и перекрывает режим (ARCH §8 п.3: локальный
 * вход, приоритет выше любого режима СУЛ, работает и без связи со станцией).
 * Но перекрывает он именно СЛОТ, а не экран: номер этажа и стрелка остаются
 * видимы. Текстовая метка — только деградация, при наличии спрайта не
 * дублируется (согласовано).
 */
static void render_status_slot(sul_mode_t mode, dispatcher_indication_t dispatcher)
{
    const fallback_sprite_id_t DISPATCHER_ID = fallback_sprite_for_dispatcher(dispatcher);
    if (DISPATCHER_ID != FALLBACK_SPRITE_NONE)
    {
        if (!draw_sprite_at(DISPATCHER_ID, MODE_CENTER_X, BAND_CENTER_Y))
        {
            draw_text_at(&SystemFont, dispatcher_label(dispatcher), MODE_CENTER_X, BAND_CENTER_Y);
        }
        return;
    }

    const fallback_sprite_id_t MODE_ID = fallback_sprite_for_mode(mode);
    if (MODE_ID == FALLBACK_SPRITE_NONE)
    {
        return; /* НОРМА — значка нет */
    }

    if (!draw_sprite_at(MODE_ID, MODE_CENTER_X, BAND_CENTER_Y))
    {
        const char *p_label = mode_label(mode);
        if (p_label != NULL)
        {
            draw_text_at(&SystemFont, p_label, MODE_CENTER_X, BAND_CENTER_Y);
        }
    }
}

static void render(sul_mode_t mode, const sul_result_t *p_result,
                   dispatcher_indication_t dispatcher)
{
    gfx_clear();

    /* Этаж — ВСЕГДА, независимо от режима и от диспетчерского входа
     * (согласовано перед 4.1: перекрывать номер этажа нельзя). */
    draw_text_at(&FloorFontFallback, p_result->pos, FLOOR_CENTER_X, FLOOR_Y);

    /* Левый слот: во время ВРЕМЕННОЙ погрузки кабина стоит — там обратный
     * отсчёт вместо стрелки (`lading_secs > 0` отличает временную погрузку от
     * инструментальной). Конфликта по существу нет: стоящей кабине направление
     * не нужно. */
    if ((mode == SUL_MODE_LADING) && (p_result->lading_secs > 0U))
    {
        char secs[6];
        (void) snprintf(secs, sizeof(secs), "%u", (unsigned) p_result->lading_secs);
        draw_text_at(&FloorFontFallback, secs, ARROW_CENTER_X, FLOOR_Y);
    }
    else
    {
        render_direction(p_result->direction);
    }

    render_status_slot(mode, dispatcher);
}

void ui_fallback_render_initial(const sul_result_t *p_result, dispatcher_indication_t dispatcher)
{
    render(sul_resolve_mode(p_result), p_result, dispatcher);
}

void ui_fallback_render(const indication_task_t *p_task, const sul_result_t *p_result,
                        dispatcher_indication_t dispatcher)
{
    /* Перерисовываем на изменение того, что fallback реально показывает:
     * позиция, стрелка, режим (+ отсчёт погрузки идёт вместе с mode/pos).
     * next-этаж в safe-mode не рисуется (богатый layout — Фаза 5), поэтому
     * на next_pending не будим. Смена dispatcher сюда не входит — она не
     * приходит с этим diff'ом вообще (свой путь пробуждения render_task,
     * см. task_render.c) — только пока mode/pos/direction ТОЖЕ изменились
     * в этом же кадре, dispatcher едет попутно через параметр. */
    if (!p_task->pos_pending && !p_task->direction_pending && !p_task->mode_pending)
    {
        return;
    }
    render(p_task->mode, p_result, dispatcher);
}
