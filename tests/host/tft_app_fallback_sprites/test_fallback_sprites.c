/**
 * @file  test_fallback_sprites.c
 * @brief Host-тесты выбора спрайта резервного режима (ui/fallback/src/
 *        fallback_sprites.c, Фаза 4.1).
 *
 * Модуль чистый — ни gfx, ни bsp, ни указателей на картинки, только
 * «состояние → id». Поэтому проверяется здесь целиком, по образцу
 * `sul_resolve_mode()`. Сама отрисовка (id → картинка → пиксели) проверяется
 * на стенде: привязка id к настоящим `tImage` живёт в отдельной единице
 * трансляции.
 *
 * Главная задача набора — не «проверить switch», а ЗАКРЫТЬ КЛАСС ОШИБКИ
 * «добавили режим в домен, забыли спрайт»: PLAN уже фиксирует долг по режиму
 * «перевозка лежачих больных» (Фаза 8), и без теста забытая строка вылезла бы
 * пустым экраном на объекте.
 */

#include "ui/fallback_sprites.h"
#include "unity.h"

#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

/* ── Направление ─────────────────────────────────────────────────────────── */

static void test_direction_maps_to_its_arrow(void)
{
    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_ARROW_UP, fallback_sprite_for_direction(SUL_DIR_UP));
    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_ARROW_DOWN, fallback_sprite_for_direction(SUL_DIR_DOWN));
    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_ARROW_DOUBLE, fallback_sprite_for_direction(SUL_DIR_DOUBLE));
}

static void test_standing_still_needs_no_arrow(void)
{
    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_NONE, fallback_sprite_for_direction(SUL_DIR_NONE));
}

static void test_double_arrow_has_its_own_sprite(void)
{
    /* В примитивном рендере Фазы 1 двойная стрелка не рисовалась вовсе. Это
     * было ограничением примитива, а не решением — фиксируем, что теперь у
     * неё свой спрайт, отличный от одиночных. */
    const fallback_sprite_id_t DOUBLE = fallback_sprite_for_direction(SUL_DIR_DOUBLE);

    TEST_ASSERT_NOT_EQUAL(FALLBACK_SPRITE_NONE, DOUBLE);
    TEST_ASSERT_NOT_EQUAL(fallback_sprite_for_direction(SUL_DIR_UP), DOUBLE);
    TEST_ASSERT_NOT_EQUAL(fallback_sprite_for_direction(SUL_DIR_DOWN), DOUBLE);
}

/* ── Режимы ──────────────────────────────────────────────────────────────── */

static void test_normal_mode_needs_no_pictogram(void)
{
    /* НОРМА — это «рисуем этаж», а не «рисуем значок нормы». */
    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_NONE, fallback_sprite_for_mode(SUL_MODE_NORMAL));
}

static void test_every_mode_maps_to_a_sprite(void)
{
    /* ЗАЩИТА ОТ ЗАБЫТОЙ СТРОКИ. Новый режим в домене без строки в таблице
     * получит нулевую инициализацию (= NONE) и молча перестанет показываться —
     * ровно то, что на объекте выглядит как «экран пустой». Перечисление
     * ЯВНОЕ, а не циклом до COUNT: у sul_mode_t нет терминатора, и добавление
     * режима обязано потребовать правки И таблицы, И этого теста. */
    static const sul_mode_t MODES[] = {
        SUL_MODE_LADING,     SUL_MODE_MAINTENANCE, SUL_MODE_SEISMIC,
        SUL_MODE_OVERLOAD,   SUL_MODE_FIRE_ALARM,  SUL_MODE_FIREMAN,
        SUL_MODE_EVACUATION, SUL_MODE_ERROR,
    };

    for (size_t i = 0U; i < (sizeof(MODES) / sizeof(MODES[0])); i++)
    {
        TEST_ASSERT_NOT_EQUAL(FALLBACK_SPRITE_NONE, fallback_sprite_for_mode(MODES[i]));
    }
}

static void test_modes_do_not_share_sprites(void)
{
    /* Разные режимы обязаны быть различимы на экране: общий спрайт у двух
     * режимов — это ошибка привязки, а не экономия. */
    static const sul_mode_t MODES[] = {
        SUL_MODE_LADING,     SUL_MODE_MAINTENANCE, SUL_MODE_SEISMIC,
        SUL_MODE_OVERLOAD,   SUL_MODE_FIRE_ALARM,  SUL_MODE_FIREMAN,
        SUL_MODE_EVACUATION, SUL_MODE_ERROR,
    };
    const size_t N = sizeof(MODES) / sizeof(MODES[0]);

    for (size_t i = 0U; i < N; i++)
    {
        for (size_t j = i + 1U; j < N; j++)
        {
            TEST_ASSERT_NOT_EQUAL(fallback_sprite_for_mode(MODES[i]),
                                  fallback_sprite_for_mode(MODES[j]));
        }
    }
}

/* ── Диспетчер ───────────────────────────────────────────────────────────── */

static void test_dispatcher_states_map_to_distinct_sprites(void)
{
    const fallback_sprite_id_t CALLING =
        fallback_sprite_for_dispatcher(DISPATCHER_INDICATION_CALLING);
    const fallback_sprite_id_t TALKING =
        fallback_sprite_for_dispatcher(DISPATCHER_INDICATION_TALKING);

    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_NONE,
                      fallback_sprite_for_dispatcher(DISPATCHER_INDICATION_NONE));
    TEST_ASSERT_NOT_EQUAL(FALLBACK_SPRITE_NONE, CALLING);
    TEST_ASSERT_NOT_EQUAL(FALLBACK_SPRITE_NONE, TALKING);
    TEST_ASSERT_NOT_EQUAL(CALLING, TALKING);
}

static void test_dispatcher_sprites_do_not_collide_with_modes(void)
{
    /* Диспетчерский вход перекрывает любой режим СУЛ (§3.4) — если бы он делил
     * спрайт с режимом, на экране было бы не отличить «пожар» от «вызов». */
    TEST_ASSERT_NOT_EQUAL(fallback_sprite_for_mode(SUL_MODE_FIRE_ALARM),
                          fallback_sprite_for_dispatcher(DISPATCHER_INDICATION_CALLING));
    TEST_ASSERT_NOT_EQUAL(fallback_sprite_for_mode(SUL_MODE_ERROR),
                          fallback_sprite_for_dispatcher(DISPATCHER_INDICATION_TALKING));
}

/* ── Мусор на входе ──────────────────────────────────────────────────────── */

static void test_out_of_range_values_are_silent_none(void)
{
    /* Декодер протокола — чужой код (§8 ARCH); презентация обязана не падать
     * на неожиданном значении, а просто не рисовать значок. */
    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_NONE, fallback_sprite_for_direction((sul_direction_t) 99));
    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_NONE, fallback_sprite_for_mode((sul_mode_t) 99));
    TEST_ASSERT_EQUAL(FALLBACK_SPRITE_NONE,
                      fallback_sprite_for_dispatcher((dispatcher_indication_t) 99));
}

/* ── Имена ───────────────────────────────────────────────────────────────── */

static void test_every_sprite_has_a_unique_name(void)
{
    /* Имя спрайта — это ещё и имя PNG в каталоге авторинга и имя символа в
     * сгенерированном .c. Дубликат означал бы, что две разные картинки едут в
     * один файл — паковщик молча перезапишет одну другой. */
    for (int i = 0; i < (int) FALLBACK_SPRITE_COUNT; i++)
    {
        const char *p_name = fallback_sprite_name((fallback_sprite_id_t) i);
        TEST_ASSERT_NOT_NULL(p_name);
        TEST_ASSERT_TRUE(strlen(p_name) > 0U);

        for (int j = i + 1; j < (int) FALLBACK_SPRITE_COUNT; j++)
        {
            TEST_ASSERT_FALSE(strcmp(p_name, fallback_sprite_name((fallback_sprite_id_t) j)) == 0);
        }
    }
}

static void test_unknown_id_reports_none(void)
{
    TEST_ASSERT_EQUAL_STRING("none", fallback_sprite_name((fallback_sprite_id_t) 12345));
    TEST_ASSERT_EQUAL_STRING("none", fallback_sprite_name(FALLBACK_SPRITE_COUNT));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_direction_maps_to_its_arrow);
    RUN_TEST(test_standing_still_needs_no_arrow);
    RUN_TEST(test_double_arrow_has_its_own_sprite);

    RUN_TEST(test_normal_mode_needs_no_pictogram);
    RUN_TEST(test_every_mode_maps_to_a_sprite);
    RUN_TEST(test_modes_do_not_share_sprites);

    RUN_TEST(test_dispatcher_states_map_to_distinct_sprites);
    RUN_TEST(test_dispatcher_sprites_do_not_collide_with_modes);

    RUN_TEST(test_out_of_range_values_are_silent_none);

    RUN_TEST(test_every_sprite_has_a_unique_name);
    RUN_TEST(test_unknown_id_reports_none);

    return UNITY_END();
}
