/*
 * test_bsp_button.c — host unit-тесты для bsp_button
 *
 * Категория B: button.c использует fsl_gpio.h.
 * GPIO_ReadPinInput мокируется через fff.
 * GPIO_PinInit не вызывается (bsp_button_init не трогает GPIO) — фейк не нужен.
 */

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS; /* ровно один раз на весь .c файл */

#include "fsl_gpio.h" /* stub из tests/host/mocks/ */

/* Мок единственной SDK-функции, которую вызывает button.c */
FAKE_VALUE_FUNC(uint32_t, GPIO_ReadPinInput, GPIO_Type *, uint32_t);

#include "bsp/button.h" /* тестируемый модуль — последним */

/* -------------------------------------------------------------------------
 * setUp / tearDown
 * ---------------------------------------------------------------------- */

void setUp(void)
{
    RESET_FAKE(GPIO_ReadPinInput);
    FFF_RESET_HISTORY();
    /* Дефолт: кнопки не нажаты (пин HIGH) */
    GPIO_ReadPinInput_fake.return_val = 1U;
    bsp_button_init();
}

void tearDown(void)
{
}

/* -------------------------------------------------------------------------
 * Вспомогательные функции
 * ---------------------------------------------------------------------- */

/** Выполнить N вызовов bsp_button_poll() с одним и тем же raw-значением.
 *  Удобно для набора debounce-счётчика. */
static void poll_n(uint32_t raw_val, uint32_t n)
{
    GPIO_ReadPinInput_fake.return_val = raw_val;
    for (uint32_t i = 0U; i < n; i++)
    {
        bsp_button_poll();
    }
}

/* -------------------------------------------------------------------------
 * Инициализация
 * ---------------------------------------------------------------------- */

void test_init_returns_ok(void)
{
    bsp_status_t status = bsp_button_init();
    TEST_ASSERT_EQUAL(BSP_OK, status);
}

void test_init_clears_state(void)
{
    /* Накопить состояние */
    poll_n(0U, 4U); /* 4 × LOW — debounce сработал */
    TEST_ASSERT_TRUE(bsp_button_is_pressed(BSP_BUTTON_1));

    /* Реинициализация должна сбросить всё */
    bsp_button_init();
    TEST_ASSERT_FALSE(bsp_button_is_pressed(BSP_BUTTON_1));
    TEST_ASSERT_FALSE(bsp_button_get_event_pressed(BSP_BUTTON_1));
}

/* -------------------------------------------------------------------------
 * Сырое чтение (bsp_button_read)
 * ---------------------------------------------------------------------- */

void test_read_returns_true_when_pin_low(void)
{
    GPIO_ReadPinInput_fake.return_val = 0U; /* LOW = нажата */
    TEST_ASSERT_TRUE(bsp_button_read(BSP_BUTTON_1));
}

void test_read_returns_false_when_pin_high(void)
{
    GPIO_ReadPinInput_fake.return_val = 1U; /* HIGH = не нажата */
    TEST_ASSERT_FALSE(bsp_button_read(BSP_BUTTON_1));
}

void test_read_invalid_index_returns_false(void)
{
    TEST_ASSERT_FALSE(bsp_button_read((bsp_button_t) BSP_BUTTON_COUNT));
    TEST_ASSERT_FALSE(bsp_button_read((bsp_button_t) 99U));
}

/* -------------------------------------------------------------------------
 * Debounce — событие нажатия
 * ---------------------------------------------------------------------- */

void test_debounce_no_event_before_threshold(void)
{
    /* 3 сэмпла LOW — порог 4 не достигнут */
    poll_n(0U, 3U);
    TEST_ASSERT_FALSE(bsp_button_get_event_pressed(BSP_BUTTON_1));
    TEST_ASSERT_FALSE(bsp_button_is_pressed(BSP_BUTTON_1));
}

void test_debounce_event_fires_on_4th_sample(void)
{
    poll_n(0U, 4U);
    TEST_ASSERT_TRUE(bsp_button_get_event_pressed(BSP_BUTTON_1));
}

void test_debounce_stable_state_after_threshold(void)
{
    poll_n(0U, 4U);
    TEST_ASSERT_TRUE(bsp_button_is_pressed(BSP_BUTTON_1));
}

void test_debounce_no_duplicate_event_on_hold(void)
{
    /* Порог достигнут, затем кнопка удерживается ещё много раз */
    poll_n(0U, 4U);
    (void) bsp_button_get_event_pressed(BSP_BUTTON_1); /* сброс флага */

    poll_n(0U, 10U); /* удержание */
    TEST_ASSERT_FALSE(bsp_button_get_event_pressed(BSP_BUTTON_1));
}

/* -------------------------------------------------------------------------
 * Потребление события
 * ---------------------------------------------------------------------- */

void test_event_consumed_after_get(void)
{
    poll_n(0U, 4U);
    TEST_ASSERT_TRUE(bsp_button_get_event_pressed(BSP_BUTTON_1));
    /* Второй вызов — флаг уже сброшен */
    TEST_ASSERT_FALSE(bsp_button_get_event_pressed(BSP_BUTTON_1));
}

/* -------------------------------------------------------------------------
 * Debounce — событие отпускания
 * ---------------------------------------------------------------------- */

void test_debounce_released_event(void)
{
    /* Нажать */
    poll_n(0U, 4U);
    (void) bsp_button_get_event_pressed(BSP_BUTTON_1);

    /* Отпустить */
    poll_n(1U, 4U);
    TEST_ASSERT_TRUE(bsp_button_get_event_released(BSP_BUTTON_1));
    TEST_ASSERT_FALSE(bsp_button_is_pressed(BSP_BUTTON_1));
}

void test_debounce_released_event_consumed(void)
{
    poll_n(0U, 4U);
    poll_n(1U, 4U);
    TEST_ASSERT_TRUE(bsp_button_get_event_released(BSP_BUTTON_1));
    TEST_ASSERT_FALSE(bsp_button_get_event_released(BSP_BUTTON_1));
}

/* -------------------------------------------------------------------------
 * Сброс счётчика при глитче
 * ---------------------------------------------------------------------- */

void test_counter_reset_on_glitch(void)
{
    /* 3 сэмпла LOW — почти порог */
    poll_n(0U, 3U);
    /* Один HIGH — глитч, счётчик сбрасывается */
    poll_n(1U, 1U);
    /* Ещё 3 LOW — снова не хватает одного */
    poll_n(0U, 3U);
    TEST_ASSERT_FALSE(bsp_button_get_event_pressed(BSP_BUTTON_1));
    TEST_ASSERT_FALSE(bsp_button_is_pressed(BSP_BUTTON_1));

    /* Четвёртый сэмпл LOW — теперь порог достигнут */
    poll_n(0U, 1U);
    TEST_ASSERT_TRUE(bsp_button_get_event_pressed(BSP_BUTTON_1));
}

/* -------------------------------------------------------------------------
 * Независимость кнопок
 * ---------------------------------------------------------------------- */

/*
 * GPIO_ReadPinInput вызывается для обеих кнопок в одном bsp_button_poll().
 * Чтобы задать разные значения для BTN1 и BTN2, используем return_val_seq:
 * каждый последующий вызов возвращает следующий элемент массива.
 * При poll_n(n=4): 4 вызова × 2 кнопки = 8 значений в последовательности.
 */
void test_no_crosstalk_btn2_pressed_btn1_idle(void)
{
    /* Чередуем: BTN1=HIGH(не нажата), BTN2=LOW(нажата) */
    uint32_t seq[] = {
        1U, 0U, /* poll 1: btn1=HIGH, btn2=LOW */
        1U, 0U, /* poll 2 */
        1U, 0U, /* poll 3 */
        1U, 0U, /* poll 4 */
    };
    SET_RETURN_SEQ(GPIO_ReadPinInput, seq, 8);

    for (uint32_t i = 0U; i < 4U; i++)
    {
        bsp_button_poll();
    }

    TEST_ASSERT_FALSE(bsp_button_get_event_pressed(BSP_BUTTON_1));
    TEST_ASSERT_TRUE(bsp_button_get_event_pressed(BSP_BUTTON_2));
}

void test_both_buttons_independent_state(void)
{
    /* Нажать BTN1, не нажимать BTN2 */
    uint32_t seq_press[] = {
        0U, 1U, 0U, 1U, 0U, 1U, 0U, 1U,
    };
    SET_RETURN_SEQ(GPIO_ReadPinInput, seq_press, 8);
    for (uint32_t i = 0U; i < 4U; i++)
    {
        bsp_button_poll();
    }

    TEST_ASSERT_TRUE(bsp_button_is_pressed(BSP_BUTTON_1));
    TEST_ASSERT_FALSE(bsp_button_is_pressed(BSP_BUTTON_2));
}

/* -------------------------------------------------------------------------
 * Граничные значения индекса
 * ---------------------------------------------------------------------- */

void test_is_pressed_invalid_index_returns_false(void)
{
    TEST_ASSERT_FALSE(bsp_button_is_pressed((bsp_button_t) BSP_BUTTON_COUNT));
}

void test_get_event_pressed_invalid_index_returns_false(void)
{
    TEST_ASSERT_FALSE(bsp_button_get_event_pressed((bsp_button_t) 99U));
}

void test_get_event_released_invalid_index_returns_false(void)
{
    TEST_ASSERT_FALSE(bsp_button_get_event_released((bsp_button_t) 99U));
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    /* Инициализация */
    RUN_TEST(test_init_returns_ok);
    RUN_TEST(test_init_clears_state);

    /* Сырое чтение */
    RUN_TEST(test_read_returns_true_when_pin_low);
    RUN_TEST(test_read_returns_false_when_pin_high);
    RUN_TEST(test_read_invalid_index_returns_false);

    /* Debounce — нажатие */
    RUN_TEST(test_debounce_no_event_before_threshold);
    RUN_TEST(test_debounce_event_fires_on_4th_sample);
    RUN_TEST(test_debounce_stable_state_after_threshold);
    RUN_TEST(test_debounce_no_duplicate_event_on_hold);

    /* Потребление */
    RUN_TEST(test_event_consumed_after_get);

    /* Debounce — отпускание */
    RUN_TEST(test_debounce_released_event);
    RUN_TEST(test_debounce_released_event_consumed);

    /* Глитч */
    RUN_TEST(test_counter_reset_on_glitch);

    /* Независимость */
    RUN_TEST(test_no_crosstalk_btn2_pressed_btn1_idle);
    RUN_TEST(test_both_buttons_independent_state);

    /* Граничные индексы */
    RUN_TEST(test_is_pressed_invalid_index_returns_false);
    RUN_TEST(test_get_event_pressed_invalid_index_returns_false);
    RUN_TEST(test_get_event_released_invalid_index_returns_false);

    return UNITY_END();
}