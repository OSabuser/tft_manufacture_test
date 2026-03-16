#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS;

/* ── Моки GPIO-функций ───────────────────────────────────────────────
 * Должны быть объявлены ДО включения тестируемого модуля,
 * чтобы fff-заглушки заменили реальные функции при линковке.
 * ─────────────────────────────────────────────────────────────────── */
#include "fsl_gpio.h"
FAKE_VOID_FUNC(GPIO_PinInit, GPIO_Type *, uint32_t, const gpio_pin_config_t *);
FAKE_VOID_FUNC(GPIO_PinWrite, GPIO_Type *, uint32_t, uint8_t);

/* ── Capture для gpio_pin_config_t ──────────────────────────────────
 * fff хранит arg2_history как указатель — но cfg живёт на стеке
 * led_init() и становится dangling после возврата.
 * (ASAN правильно детектирует это как stack-use-after-return)
 *
 * Решение: custom_fake копирует структуру по значению в момент вызова,
 * пока стек led_init() ещё жив.
 * ─────────────────────────────────────────────────────────────────── */
static gpio_pin_config_t s_captured_cfg[2];
static int s_capture_idx = 0;

static void GPIO_PinInit_capture(GPIO_Type *base, uint32_t pin, const gpio_pin_config_t *cfg)
{
    (void) base;
    (void) pin;
    if (s_capture_idx < 2)
    {
        s_captured_cfg[s_capture_idx++] = *cfg; /* копируем по значению */
    }
}

/* ── Тестируемый модуль ──────────────────────────────────────────────── */
#include "bsp/led.h"

/* ── setUp / tearDown ────────────────────────────────────────────────── */

void setUp(void)
{
    RESET_FAKE(GPIO_PinInit);
    RESET_FAKE(GPIO_PinWrite);
    FFF_RESET_HISTORY();

    /* подключаем capture до вызова led_init */
    s_capture_idx                 = 0;
    GPIO_PinInit_fake.custom_fake = GPIO_PinInit_capture;

    led_init();
}

void tearDown(void)
{
}

/* ── Тесты инициализации ─────────────────────────────────────────────── */

void test_led_init_calls_gpio_init_for_each_led(void)
{
    TEST_ASSERT_EQUAL(2, GPIO_PinInit_fake.call_count);
}

void test_led_init_configures_as_output(void)
{
    /* s_captured_cfg — копия по значению, стек led_init() не нужен */
    TEST_ASSERT_EQUAL(kGPIO_DigitalOutput, s_captured_cfg[0].direction);
    TEST_ASSERT_EQUAL(kGPIO_DigitalOutput, s_captured_cfg[1].direction);
}

void test_led_init_output_logic_is_high(void)
{
    /* active LOW — начальное состояние GPIO = 1 (LED выключен) */
    TEST_ASSERT_EQUAL_UINT8(1U, s_captured_cfg[0].outputLogic);
    TEST_ASSERT_EQUAL_UINT8(1U, s_captured_cfg[1].outputLogic);
}

void test_led_init_both_leds_off(void)
{
    TEST_ASSERT_FALSE(led_get(LED_HEARTBEAT));
    TEST_ASSERT_FALSE(led_get(LED_APP));
}

/* ── Тесты led_on / led_off ──────────────────────────────────────────── */

void test_led_on_sets_state_true(void)
{
    led_on(LED_HEARTBEAT);
    TEST_ASSERT_TRUE(led_get(LED_HEARTBEAT));
}

void test_led_off_sets_state_false(void)
{
    led_on(LED_HEARTBEAT);
    led_off(LED_HEARTBEAT);
    TEST_ASSERT_FALSE(led_get(LED_HEARTBEAT));
}

void test_led_on_writes_gpio_low(void)
{
    /* active LOW: включить LED = записать 0 в GPIO */
    led_on(LED_HEARTBEAT);
    TEST_ASSERT_EQUAL_UINT8(0U, GPIO_PinWrite_fake.arg2_val);
}

void test_led_off_writes_gpio_high(void)
{
    /* active LOW: выключить LED = записать 1 в GPIO */
    led_off(LED_HEARTBEAT);
    TEST_ASSERT_EQUAL_UINT8(1U, GPIO_PinWrite_fake.arg2_val);
}

/* ── Тесты led_toggle ────────────────────────────────────────────────── */

void test_led_toggle_off_to_on(void)
{
    TEST_ASSERT_FALSE(led_get(LED_HEARTBEAT));
    led_toggle(LED_HEARTBEAT);
    TEST_ASSERT_TRUE(led_get(LED_HEARTBEAT));
}

void test_led_toggle_on_to_off(void)
{
    led_on(LED_HEARTBEAT);
    led_toggle(LED_HEARTBEAT);
    TEST_ASSERT_FALSE(led_get(LED_HEARTBEAT));
}

void test_led_toggle_twice_returns_to_initial(void)
{
    led_toggle(LED_HEARTBEAT);
    led_toggle(LED_HEARTBEAT);
    TEST_ASSERT_FALSE(led_get(LED_HEARTBEAT));
}

/* ── Тесты led_set ───────────────────────────────────────────────────── */

void test_led_set_true_turns_on(void)
{
    led_set(LED_APP, true);
    TEST_ASSERT_TRUE(led_get(LED_APP));
}

void test_led_set_false_turns_off(void)
{
    led_set(LED_APP, true);
    led_set(LED_APP, false);
    TEST_ASSERT_FALSE(led_get(LED_APP));
}

/* ── Тест независимости LED друг от друга ───────────────────────────── */

void test_leds_are_independent(void)
{
    led_on(LED_HEARTBEAT);
    TEST_ASSERT_TRUE(led_get(LED_HEARTBEAT));
    TEST_ASSERT_FALSE(led_get(LED_APP));

    led_on(LED_APP);
    led_off(LED_HEARTBEAT);
    TEST_ASSERT_FALSE(led_get(LED_HEARTBEAT));
    TEST_ASSERT_TRUE(led_get(LED_APP));
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_led_init_calls_gpio_init_for_each_led);
    RUN_TEST(test_led_init_configures_as_output);
    RUN_TEST(test_led_init_output_logic_is_high);
    RUN_TEST(test_led_init_both_leds_off);

    RUN_TEST(test_led_on_sets_state_true);
    RUN_TEST(test_led_off_sets_state_false);
    RUN_TEST(test_led_on_writes_gpio_low);
    RUN_TEST(test_led_off_writes_gpio_high);

    RUN_TEST(test_led_toggle_off_to_on);
    RUN_TEST(test_led_toggle_on_to_off);
    RUN_TEST(test_led_toggle_twice_returns_to_initial);

    RUN_TEST(test_led_set_true_turns_on);
    RUN_TEST(test_led_set_false_turns_off);

    RUN_TEST(test_leds_are_independent);

    return UNITY_END();
}