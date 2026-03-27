/*
 * test_bsp_opto.c — host-тест bsp_opto
 *
 * Категория B: opto.c вызывает fsl_gpio.h и bsp_tick.
 * Все зависимости заменены fff-фейками.
 *
 * Топология пинов (из opto.c):
 *   BSP_OPTO_CH_IN1 → GPIO1 pin 22
 *   BSP_OPTO_CH_IN2 → GPIO1 pin 21
 *   BSP_OPTO_CH_RS  → GPIO1 pin 23
 *
 * Полярность: active-HIGH (неинвертирующая оптопара).
 *   raw=1 → BSP_OPTO_STATE_ACTIVE
 *   raw=0 → BSP_OPTO_STATE_INACTIVE
 */

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS;

/* ── 1. Стабы SDK (до тестируемого модуля) ───────────────────────────────── */

#include "fsl_gpio.h"

FAKE_VOID_FUNC(GPIO_PinInit, GPIO_Type *, uint32_t, const gpio_pin_config_t *);
FAKE_VOID_FUNC(GPIO_PinWrite, GPIO_Type *, uint32_t, uint8_t);
FAKE_VALUE_FUNC(uint32_t, GPIO_PinRead, GPIO_Type *, uint32_t);

FAKE_VOID_FUNC(GPIO_SetPinInterruptConfig, GPIO_Type *, uint32_t, gpio_interrupt_mode_t);
FAKE_VOID_FUNC(GPIO_EnableInterrupts, GPIO_Type *, uint32_t);
FAKE_VOID_FUNC(GPIO_DisableInterrupts, GPIO_Type *, uint32_t);
FAKE_VALUE_FUNC(uint32_t, GPIO_GetPinsInterruptFlags, GPIO_Type *);
FAKE_VOID_FUNC(GPIO_ClearPinsInterruptFlags, GPIO_Type *, uint32_t);

FAKE_VOID_FUNC(EnableIRQ, IRQn_Type);

/* ── 2. Стабы BSP ─────────────────────────────────────────────────────────── */

#include "board.h"
FAKE_VOID_FUNC(BOARD_InitRS_GPIO);

#include "bsp/tick.h"
FAKE_VALUE_FUNC(uint32_t, bsp_tick_get_ms);

/* ── 3. Тестируемый модуль — всегда последним ────────────────────────────── */

#include "bsp/opto.h"

/* ── Вспомогательное ─────────────────────────────────────────────────────── */

#define OPTO_IN1_PIN 22U
#define OPTO_IN2_PIN 21U
#define OPTO_RS_PIN  23U

#define DEBOUNCE_MS 10U

/* Capture конфигурации GPIO_PinInit */
#define MAX_INIT_CALLS 3U
static gpio_pin_config_t s_captured_cfg[MAX_INIT_CALLS];
static uint32_t s_captured_pin[MAX_INIT_CALLS];
static uint32_t s_capture_idx;

static void GPIO_PinInit_capture(GPIO_Type *base, uint32_t pin, const gpio_pin_config_t *p_cfg)
{
    (void) base;
    if (s_capture_idx < MAX_INIT_CALLS)
    {
        s_captured_pin[s_capture_idx] = pin;
        s_captured_cfg[s_capture_idx] = *p_cfg;
        s_capture_idx++;
    }
}

/* Capture SetPinInterruptConfig — увеличен буфер: при MODE_LEVEL ISR меняет фронт */
#define MAX_IRQ_CONFIG_CALLS 8U
static gpio_interrupt_mode_t s_captured_irq_mode[MAX_IRQ_CONFIG_CALLS];
static uint32_t s_captured_irq_pin[MAX_IRQ_CONFIG_CALLS];
static uint32_t s_irq_config_idx;

static void GPIO_SetPinInterruptConfig_capture(GPIO_Type *base, uint32_t pin,
                                               gpio_interrupt_mode_t mode)
{
    (void) base;
    if (s_irq_config_idx < MAX_IRQ_CONFIG_CALLS)
    {
        s_captured_irq_pin[s_irq_config_idx]  = pin;
        s_captured_irq_mode[s_irq_config_idx] = mode;
        s_irq_config_idx++;
    }
}

/* Коллбэк-фиксатор */
static bsp_opto_ch_t s_cb_ch;
static bsp_opto_state_t s_cb_state;
static uint32_t s_cb_count;

static void test_callback(bsp_opto_ch_t ch, bsp_opto_state_t state)
{
    s_cb_ch    = ch;
    s_cb_state = state;
    s_cb_count++;
}

/*
 * Конфигурация по умолчанию:
 *   IN1, IN2 — MODE_LEVEL
 *   RS       — отключён (rs_as_gpio = false)
 */
static bsp_opto_config_t make_default_cfg(void)
{
    bsp_opto_config_t cfg = {
        .callbacks   = { test_callback, test_callback, NULL },
        .modes       = {
            [BSP_OPTO_CH_IN1] = BSP_OPTO_MODE_LEVEL,
            [BSP_OPTO_CH_IN2] = BSP_OPTO_MODE_LEVEL,
            [BSP_OPTO_CH_RS]  = BSP_OPTO_MODE_LEVEL,
        },
        .edges       = {
            [BSP_OPTO_CH_IN1] = BSP_OPTO_EDGE_RISING,
            [BSP_OPTO_CH_IN2] = BSP_OPTO_EDGE_RISING,
            [BSP_OPTO_CH_RS]  = BSP_OPTO_EDGE_RISING,
        },
        .rs_as_gpio  = false,
        .debounce_ms = DEBOUNCE_MS,
    };
    return cfg;
}

/* Forward declaration ISR — определён в opto.c */
void GPIO1_Combined_16_31_IRQHandler(void);

/* Симулировать ISR — дёргаем обработчик напрямую как из железа */
static void simulate_isr(uint8_t pin, uint32_t pin_level)
{
    GPIO_GetPinsInterruptFlags_fake.return_val = (1UL << pin);
    GPIO_PinRead_fake.return_val               = pin_level;
    GPIO1_Combined_16_31_IRQHandler();
}

/* ── setUp / tearDown ────────────────────────────────────────────────────── */

void setUp(void)
{
    RESET_FAKE(GPIO_PinInit);
    RESET_FAKE(GPIO_PinWrite);
    RESET_FAKE(GPIO_PinRead);
    RESET_FAKE(GPIO_SetPinInterruptConfig);
    RESET_FAKE(GPIO_EnableInterrupts);
    RESET_FAKE(GPIO_DisableInterrupts);
    RESET_FAKE(GPIO_GetPinsInterruptFlags);
    RESET_FAKE(GPIO_ClearPinsInterruptFlags);
    RESET_FAKE(EnableIRQ);
    RESET_FAKE(BOARD_InitRS_GPIO);
    RESET_FAKE(bsp_tick_get_ms);
    FFF_RESET_HISTORY();

    s_capture_idx    = 0U;
    s_irq_config_idx = 0U;
    s_cb_count       = 0U;

    GPIO_PinInit_fake.custom_fake               = GPIO_PinInit_capture;
    GPIO_SetPinInterruptConfig_fake.custom_fake = GPIO_SetPinInterruptConfig_capture;

    /*
     * По умолчанию пины INACTIVE.
     * active-HIGH: raw=0 → INACTIVE.
     */
    GPIO_PinRead_fake.return_val = 0U;
}

void tearDown(void)
{
}

/* ── Тесты: bsp_opto_init ────────────────────────────────────────────────── */

void test_init_returns_ok(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    TEST_ASSERT_EQUAL(BSP_OK, bsp_opto_init(&cfg));
}

void test_init_null_config_returns_err(void)
{
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_opto_init(NULL));
}

void test_init_two_channels_when_rs_as_gpio_false(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);
    /* IN1 + IN2, RS пропущен */
    TEST_ASSERT_EQUAL(2U, GPIO_PinInit_fake.call_count);
}

void test_init_three_channels_when_rs_as_gpio_true(void)
{
    bsp_opto_config_t cfg         = make_default_cfg();
    cfg.rs_as_gpio                = true;
    cfg.callbacks[BSP_OPTO_CH_RS] = test_callback;
    bsp_opto_init(&cfg);
    TEST_ASSERT_EQUAL(3U, GPIO_PinInit_fake.call_count);
}

void test_init_all_pins_configured_as_input(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);
    TEST_ASSERT_EQUAL(kGPIO_DigitalInput, s_captured_cfg[0].direction);
    TEST_ASSERT_EQUAL(kGPIO_DigitalInput, s_captured_cfg[1].direction);
}

void test_init_correct_pins_configured(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);
    /* Порядок в реестре: IN1=22, IN2=21 */
    TEST_ASSERT_EQUAL(OPTO_IN1_PIN, s_captured_pin[0]);
    TEST_ASSERT_EQUAL(OPTO_IN2_PIN, s_captured_pin[1]);
}

void test_init_irq_enabled_for_each_channel(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);
    TEST_ASSERT_EQUAL(2U, GPIO_EnableInterrupts_fake.call_count);
}

void test_init_global_irq_enabled(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);
    TEST_ASSERT_EQUAL(1U, EnableIRQ_fake.call_count);
    TEST_ASSERT_EQUAL(GPIO1_Combined_16_31_IRQn, EnableIRQ_fake.arg0_val);
}

void test_init_rs_as_gpio_calls_board_init(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    cfg.rs_as_gpio        = true;
    bsp_opto_init(&cfg);
    TEST_ASSERT_EQUAL(1U, BOARD_InitRS_GPIO_fake.call_count);
}

void test_init_rs_as_gpio_false_no_board_init(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);
    TEST_ASSERT_EQUAL(0U, BOARD_InitRS_GPIO_fake.call_count);
}

/* ── Тесты: начальный фронт MODE_LEVEL ──────────────────────────────────── */

/*
 * При init пин LOW (raw=0, INACTIVE) → ожидаем RISING фронт
 * (сигнал ещё не пришёл, ждём его появления).
 */
void test_init_level_pin_low_selects_rising_edge(void)
{
    GPIO_PinRead_fake.return_val = 0U; /* INACTIVE */
    bsp_opto_config_t cfg        = make_default_cfg();
    bsp_opto_init(&cfg);

    /* Найти первую запись для IN1 */
    uint32_t idx = 0U;
    for (uint32_t i = 0U; i < s_irq_config_idx; i++)
    {
        if (s_captured_irq_pin[i] == OPTO_IN1_PIN)
        {
            idx = i;
            break;
        }
    }
    TEST_ASSERT_EQUAL(kGPIO_IntRisingEdge, s_captured_irq_mode[idx]);
}

/*
 * При init пин HIGH (raw=1, ACTIVE) → ожидаем FALLING фронт
 * (сигнал уже активен, ждём его снятия).
 */
void test_init_level_pin_high_selects_falling_edge(void)
{
    GPIO_PinRead_fake.return_val = 1U; /* ACTIVE */
    bsp_opto_config_t cfg        = make_default_cfg();
    bsp_opto_init(&cfg);

    uint32_t idx = 0U;
    for (uint32_t i = 0U; i < s_irq_config_idx; i++)
    {
        if (s_captured_irq_pin[i] == OPTO_IN1_PIN)
        {
            idx = i;
            break;
        }
    }
    TEST_ASSERT_EQUAL(kGPIO_IntFallingEdge, s_captured_irq_mode[idx]);
}

/* ── Тесты: переключение фронта в ISR (MODE_LEVEL) ──────────────────────── */

/*
 * После RISING фронта ISR должен переключить направление на FALLING,
 * чтобы поймать возврат сигнала.
 */
void test_isr_level_toggles_edge_after_rising(void)
{
    GPIO_PinRead_fake.return_val = 0U; /* init: pin LOW → начинаем с RISING */
    bsp_opto_config_t cfg        = make_default_cfg();
    bsp_opto_init(&cfg);

    uint32_t irq_calls_after_init = s_irq_config_idx;

    /* ISR: RISING фронт (pin стал HIGH) */
    simulate_isr(OPTO_IN1_PIN, 1U);

    /* Должна появиться новая запись SetPinInterruptConfig с FALLING */
    TEST_ASSERT_GREATER_THAN(irq_calls_after_init, s_irq_config_idx);
    TEST_ASSERT_EQUAL(kGPIO_IntFallingEdge, s_captured_irq_mode[s_irq_config_idx - 1U]);
}

/*
 * После FALLING фронта ISR должен переключить направление обратно на RISING.
 */
void test_isr_level_toggles_edge_after_falling(void)
{
    GPIO_PinRead_fake.return_val = 1U; /* init: pin HIGH → начинаем с FALLING */
    bsp_opto_config_t cfg        = make_default_cfg();
    bsp_opto_init(&cfg);

    uint32_t irq_calls_after_init = s_irq_config_idx;

    /* ISR: FALLING фронт (pin стал LOW) */
    simulate_isr(OPTO_IN1_PIN, 0U);

    TEST_ASSERT_GREATER_THAN(irq_calls_after_init, s_irq_config_idx);
    TEST_ASSERT_EQUAL(kGPIO_IntRisingEdge, s_captured_irq_mode[s_irq_config_idx - 1U]);
}

/* ── Тесты: bsp_opto_read — начальное состояние ─────────────────────────── */

void test_read_initial_inactive_when_pin_low(void)
{
    /* active-HIGH: raw=0 → INACTIVE */
    GPIO_PinRead_fake.return_val = 0U;
    bsp_opto_config_t cfg        = make_default_cfg();
    bsp_opto_init(&cfg);

    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_INACTIVE, bsp_opto_read(BSP_OPTO_CH_IN1));
    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_INACTIVE, bsp_opto_read(BSP_OPTO_CH_IN2));
}

void test_read_initial_active_when_pin_high(void)
{
    /* active-HIGH: raw=1 → ACTIVE */
    GPIO_PinRead_fake.return_val = 1U;
    bsp_opto_config_t cfg        = make_default_cfg();
    bsp_opto_init(&cfg);

    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_ACTIVE, bsp_opto_read(BSP_OPTO_CH_IN1));
}

void test_read_disabled_rs_channel_returns_inactive(void)
{
    bsp_opto_config_t cfg = make_default_cfg(); /* rs_as_gpio = false */
    bsp_opto_init(&cfg);
    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_INACTIVE, bsp_opto_read(BSP_OPTO_CH_RS));
}

void test_read_invalid_channel_returns_inactive(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);
    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_INACTIVE, bsp_opto_read((bsp_opto_ch_t) BSP_OPTO_CH_COUNT));
}

/* ── Тесты: bsp_opto_process — дебаунс (MODE_LEVEL) ─────────────────────── */

void test_process_before_debounce_no_callback(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);

    /* ISR: RISING фронт в T=100 (pin стал HIGH = ACTIVE) */
    bsp_tick_get_ms_fake.return_val = 100U;
    simulate_isr(OPTO_IN1_PIN, 1U);

    /* process в T=105: прошло 5ms < debounce_ms(10) */
    bsp_tick_get_ms_fake.return_val = 105U;
    GPIO_PinRead_fake.return_val    = 1U;
    bsp_opto_process();

    TEST_ASSERT_EQUAL(0U, s_cb_count);
}

void test_process_after_debounce_fires_callback(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);

    bsp_tick_get_ms_fake.return_val = 100U;
    simulate_isr(OPTO_IN1_PIN, 1U); /* RISING → ACTIVE */

    /* process в T=112: прошло 12ms >= debounce_ms(10) */
    bsp_tick_get_ms_fake.return_val = 112U;
    GPIO_PinRead_fake.return_val    = 1U;
    bsp_opto_process();

    TEST_ASSERT_EQUAL(1U, s_cb_count);
}

void test_process_callback_receives_correct_channel(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);

    bsp_tick_get_ms_fake.return_val = 0U;
    simulate_isr(OPTO_IN1_PIN, 1U);

    bsp_tick_get_ms_fake.return_val = DEBOUNCE_MS + 1U;
    GPIO_PinRead_fake.return_val    = 1U;
    bsp_opto_process();

    TEST_ASSERT_EQUAL(BSP_OPTO_CH_IN1, s_cb_ch);
}

void test_process_callback_receives_active_state(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);

    bsp_tick_get_ms_fake.return_val = 0U;
    simulate_isr(OPTO_IN1_PIN, 1U); /* raw=1 → ACTIVE */

    bsp_tick_get_ms_fake.return_val = DEBOUNCE_MS + 1U;
    GPIO_PinRead_fake.return_val    = 1U;
    bsp_opto_process();

    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_ACTIVE, s_cb_state);
}

void test_process_callback_receives_inactive_state(void)
{
    /* Начинаем с ACTIVE: init с pin HIGH */
    GPIO_PinRead_fake.return_val = 1U;
    bsp_opto_config_t cfg        = make_default_cfg();
    bsp_opto_init(&cfg);

    /* FALLING фронт: pin стал LOW → INACTIVE */
    bsp_tick_get_ms_fake.return_val = 0U;
    simulate_isr(OPTO_IN1_PIN, 0U);

    bsp_tick_get_ms_fake.return_val = DEBOUNCE_MS + 1U;
    GPIO_PinRead_fake.return_val    = 0U;
    bsp_opto_process();

    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_INACTIVE, s_cb_state);
}

void test_process_no_callback_if_state_unchanged(void)
{
    /* Init с pin HIGH → ACTIVE */
    GPIO_PinRead_fake.return_val = 1U;
    bsp_opto_config_t cfg        = make_default_cfg();
    bsp_opto_init(&cfg);

    /* ISR срабатывает, но пин при перечитке всё ещё ACTIVE */
    bsp_tick_get_ms_fake.return_val = 0U;
    simulate_isr(OPTO_IN1_PIN, 1U);

    bsp_tick_get_ms_fake.return_val = DEBOUNCE_MS + 1U;
    GPIO_PinRead_fake.return_val    = 1U; /* состояние не изменилось */
    bsp_opto_process();

    TEST_ASSERT_EQUAL(0U, s_cb_count);
}

void test_process_pending_cleared_after_debounce(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);

    bsp_tick_get_ms_fake.return_val = 0U;
    simulate_isr(OPTO_IN1_PIN, 1U);

    bsp_tick_get_ms_fake.return_val = DEBOUNCE_MS + 1U;
    GPIO_PinRead_fake.return_val    = 1U;
    bsp_opto_process();

    /* Второй вызов process без нового ISR — коллбэк не должен стрелять снова */
    bsp_opto_process();

    TEST_ASSERT_EQUAL(1U, s_cb_count);
}

/* ── Тесты: независимость каналов ───────────────────────────────────────── */

void test_channels_are_independent(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);

    /* ISR только для IN1 */
    bsp_tick_get_ms_fake.return_val = 0U;
    simulate_isr(OPTO_IN1_PIN, 1U);

    bsp_tick_get_ms_fake.return_val = DEBOUNCE_MS + 1U;
    GPIO_PinRead_fake.return_val    = 1U;
    bsp_opto_process();

    /* IN1 изменился (INACTIVE→ACTIVE), IN2 — нет */
    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_ACTIVE, bsp_opto_read(BSP_OPTO_CH_IN1));
    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_INACTIVE, bsp_opto_read(BSP_OPTO_CH_IN2));
    TEST_ASSERT_EQUAL(1U, s_cb_count); /* ровно один коллбэк */
}

/* ── Тесты: MODE_PROTO ───────────────────────────────────────────────────── */

/*
 * Вспомогательная конфигурация: IN1/IN2 — LEVEL, RS — PROTO.
 */
static bsp_opto_config_t make_proto_cfg(void)
{
    bsp_opto_config_t cfg = {
        .callbacks   = { test_callback, test_callback, test_callback },
        .modes       = {
            [BSP_OPTO_CH_IN1] = BSP_OPTO_MODE_LEVEL,
            [BSP_OPTO_CH_IN2] = BSP_OPTO_MODE_LEVEL,
            [BSP_OPTO_CH_RS]  = BSP_OPTO_MODE_PROTO,
        },
        .edges       = {
            [BSP_OPTO_CH_IN1] = BSP_OPTO_EDGE_RISING,
            [BSP_OPTO_CH_IN2] = BSP_OPTO_EDGE_RISING,
            [BSP_OPTO_CH_RS]  = BSP_OPTO_EDGE_RISING,
        },
        .rs_as_gpio  = true,
        .debounce_ms = DEBOUNCE_MS,
    };
    return cfg;
}

/*
 * ISR для MODE_PROTO должен немедленно вызвать коллбэк,
 * не дожидаясь bsp_opto_process().
 */
void test_proto_isr_fires_callback_immediately(void)
{
    bsp_opto_config_t cfg = make_proto_cfg();
    bsp_opto_init(&cfg);

    /* ISR: RISING фронт на RS */
    simulate_isr(OPTO_RS_PIN, 1U);

    /* Коллбэк должен уже сработать — process ещё не вызывался */
    TEST_ASSERT_EQUAL(1U, s_cb_count);
    TEST_ASSERT_EQUAL(BSP_OPTO_CH_RS, s_cb_ch);
    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_ACTIVE, s_cb_state);
}

/*
 * После ISR прерывание RS должно быть отключено
 * (GPIO_DisableInterrupts вызван с маской 1<<23).
 */
void test_proto_isr_disables_irq_after_callback(void)
{
    bsp_opto_config_t cfg = make_proto_cfg();
    bsp_opto_init(&cfg);

    simulate_isr(OPTO_RS_PIN, 1U);

    TEST_ASSERT_EQUAL(1U, GPIO_DisableInterrupts_fake.call_count);
    TEST_ASSERT_EQUAL(1UL << OPTO_RS_PIN, GPIO_DisableInterrupts_fake.arg1_val);
}

/*
 * bsp_opto_process() НЕ должен вызывать коллбэк для MODE_PROTO каналов —
 * они обрабатываются в ISR.
 */
void test_proto_process_does_not_fire_callback(void)
{
    bsp_opto_config_t cfg = make_proto_cfg();
    bsp_opto_init(&cfg);

    /* Сбрасываем счётчик после ISR */
    simulate_isr(OPTO_RS_PIN, 1U);
    s_cb_count = 0U;

    /* process не должен добавить ещё один вызов */
    bsp_tick_get_ms_fake.return_val = DEBOUNCE_MS + 1U;
    bsp_opto_process();

    TEST_ASSERT_EQUAL(0U, s_cb_count);
}

/*
 * bsp_opto_proto_arm() должен перевзвести прерывание RS:
 * вызвать GPIO_ClearPinsInterruptFlags + GPIO_EnableInterrupts
 * + GPIO_SetPinInterruptConfig для нужного пина.
 */
void test_proto_arm_reenables_irq(void)
{
    bsp_opto_config_t cfg = make_proto_cfg();
    bsp_opto_init(&cfg);

    simulate_isr(OPTO_RS_PIN, 1U);

    /* Сбрасываем счётчики — смотрим только на arm */
    RESET_FAKE(GPIO_EnableInterrupts);
    RESET_FAKE(GPIO_ClearPinsInterruptFlags);

    bsp_opto_proto_arm(BSP_OPTO_CH_RS);

    TEST_ASSERT_EQUAL(1U, GPIO_EnableInterrupts_fake.call_count);
    TEST_ASSERT_EQUAL(1U, GPIO_ClearPinsInterruptFlags_fake.call_count);
}

/*
 * bsp_opto_proto_arm() для канала MODE_LEVEL — no-op,
 * не должен трогать прерывания.
 */
void test_proto_arm_noop_for_level_channel(void)
{
    bsp_opto_config_t cfg = make_default_cfg();
    bsp_opto_init(&cfg);

    RESET_FAKE(GPIO_EnableInterrupts);
    RESET_FAKE(GPIO_ClearPinsInterruptFlags);

    bsp_opto_proto_arm(BSP_OPTO_CH_IN1);

    TEST_ASSERT_EQUAL(0U, GPIO_EnableInterrupts_fake.call_count);
    TEST_ASSERT_EQUAL(0U, GPIO_ClearPinsInterruptFlags_fake.call_count);
}

/*
 * bsp_opto_read() для канала MODE_PROTO всегда возвращает INACTIVE —
 * состояние RS отслеживается коллбэком из ISR.
 */
void test_proto_read_always_returns_inactive(void)
{
    GPIO_PinRead_fake.return_val = 1U; /* пин HIGH */
    bsp_opto_config_t cfg        = make_proto_cfg();
    bsp_opto_init(&cfg);

    TEST_ASSERT_EQUAL(BSP_OPTO_STATE_INACTIVE, bsp_opto_read(BSP_OPTO_CH_RS));
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    /* init */
    RUN_TEST(test_init_returns_ok);
    RUN_TEST(test_init_null_config_returns_err);
    RUN_TEST(test_init_two_channels_when_rs_as_gpio_false);
    RUN_TEST(test_init_three_channels_when_rs_as_gpio_true);
    RUN_TEST(test_init_all_pins_configured_as_input);
    RUN_TEST(test_init_correct_pins_configured);
    RUN_TEST(test_init_irq_enabled_for_each_channel);
    RUN_TEST(test_init_global_irq_enabled);
    RUN_TEST(test_init_rs_as_gpio_calls_board_init);
    RUN_TEST(test_init_rs_as_gpio_false_no_board_init);

    /* начальный фронт MODE_LEVEL */
    RUN_TEST(test_init_level_pin_low_selects_rising_edge);
    RUN_TEST(test_init_level_pin_high_selects_falling_edge);

    /* переключение фронта в ISR */
    RUN_TEST(test_isr_level_toggles_edge_after_rising);
    RUN_TEST(test_isr_level_toggles_edge_after_falling);

    /* read — начальное состояние */
    RUN_TEST(test_read_initial_inactive_when_pin_low);
    RUN_TEST(test_read_initial_active_when_pin_high);
    RUN_TEST(test_read_disabled_rs_channel_returns_inactive);
    RUN_TEST(test_read_invalid_channel_returns_inactive);

    /* process — дебаунс */
    RUN_TEST(test_process_before_debounce_no_callback);
    RUN_TEST(test_process_after_debounce_fires_callback);
    RUN_TEST(test_process_callback_receives_correct_channel);
    RUN_TEST(test_process_callback_receives_active_state);
    RUN_TEST(test_process_callback_receives_inactive_state);
    RUN_TEST(test_process_no_callback_if_state_unchanged);
    RUN_TEST(test_process_pending_cleared_after_debounce);

    /* независимость каналов */
    RUN_TEST(test_channels_are_independent);

    /* MODE_PROTO */
    RUN_TEST(test_proto_isr_fires_callback_immediately);
    RUN_TEST(test_proto_isr_disables_irq_after_callback);
    RUN_TEST(test_proto_process_does_not_fire_callback);
    RUN_TEST(test_proto_arm_reenables_irq);
    RUN_TEST(test_proto_arm_noop_for_level_channel);
    RUN_TEST(test_proto_read_always_returns_inactive);

    return UNITY_END();
}