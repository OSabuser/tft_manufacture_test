/**
 * @file  test_mqs.c
 * @brief Тест MQS: воспроизведение тестового тона через SAI1 + eDMA + MQS.
 *
 * Алгоритм:
 *   1. init()   — bsp_mqs_init(), генерация мелодии в s_melody_buf.
 *   2. run()    — воспроизвести мелодию (~4 с) с USB keepalive в цикле,
 *                 затем confirm_request("mqs_tone") — оператор подтверждает.
 *   3. deinit() — bsp_mqs_stop(), bsp_mqs_deinit().
 *
 * Мелодия: стерео PCM16 44100 Гц, две ноты (A4/E5) по 2 с каждая.
 * Буфер — статический глобальный в некэшируемой секции (OCRAM NonCacheable).
 * L == R (монофонический выход на плате).
 *
 * Протокол:
 *   ← confirm_request("mqs_tone", "Слышен звуковой сигнал?", 15000)
 *   → confirm("mqs_tone", true/false)
 *   ← test_result pass/fail/skip
 */

#include "bsp/mqs.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "fsl_common.h"
#include "test_module.h"
#include "test_runner.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Буфер мелодии
 * ----------------------------------------------------------------------- */

/*
 * Две ноты, по 2 с каждая: A4 (440 Гц) + E5 (659 Гц).
 * Моно-сэмплов: 44100 × 2 = 88200 на ноту.
 * Стерео-фреймов = моно-сэмплов (каждый L дублируется в R).
 * int16_t на фрейм: 2 (L + R).
 * Итого: 88200 × 2 × 2 = 352800 int16_t = 705 600 байт.
 *
 * DMA-буфер объявлен через AT_NONCACHEABLE_SECTION_ALIGN (паттерн из test_display.c).
 */
#define MQS_NOTE_SAMPLES       (44100U * 2U)
#define MQS_TOTAL_MONO_SAMPLES (MQS_NOTE_SAMPLES * 2U)
#define MQS_TOTAL_FRAMES       MQS_TOTAL_MONO_SAMPLES
#define MQS_BUF_ELEMENTS       (MQS_TOTAL_FRAMES * BSP_MQS_CHANNELS)

/*
 * DMA-буфер мелодии в некэшируемой секции OCRAM.
 * static передаётся внутрь var-аргумента макроса — единственный валидный
 * способ, так как макрос раскрывается как:
 *   __attribute__((section(...))) var __attribute__((aligned(N)))
 * Паттерн идентичен test_display.c (g_s_framebuf).
 */
static AT_NONCACHEABLE_SECTION_ALIGN(int16_t s_melody_buf[MQS_BUF_ELEMENTS], 4U);

static bool g_s_melody_ready = false;

/* --------------------------------------------------------------------------
 * Генерация мелодии (целочисленная синусоида)
 * ----------------------------------------------------------------------- */

/*
 * Таблица синуса: 96 точек на период (шаг π/48).
 * Амплитуда 22937 ≈ 0.70 × INT16_MAX — запас от клиппинга на MQS.
 * Генерируется по четвертям (таблица хранит только одну четверть).
 */
static int16_t sine_sample(uint32_t phase, uint32_t period)
{
    static const int16_t K_SIN_LUT[25] = {
        0,     2998,  5944,  8788,  11479, 13970, 16212, 18162, 19784, 21043, 21910, 22369, 22404,
        22005, 21170, 19898, 18199, 16084, 13567, 10670, 7416,  3831,  0,     -3831, -7416,
    };

    uint32_t idx     = (phase * 96U) / period % 96U;
    uint32_t quarter = idx / 24U;
    uint32_t pos     = idx % 24U;

    int16_t val;
    switch (quarter)
    {
    case 0U:
        val = K_SIN_LUT[pos];
        break;
    case 1U:
        val = K_SIN_LUT[24U - pos];
        break;
    case 2U:
        val = -K_SIN_LUT[pos];
        break;
    default:
        val = -K_SIN_LUT[24U - pos];
        break;
    }
    return val;
}

static void mqs_build_melody(void)
{
    if (g_s_melody_ready)
    {
        return;
    }

    /* A4 = 440 Гц → период ≈ 100 сэмплов при 44100 Гц */
    const uint32_t K_PERIOD_A4 = 100U;
    /* E5 = 659 Гц → период ≈ 67 сэмплов при 44100 Гц */
    const uint32_t K_PERIOD_E5 = 67U;

    uint32_t out = 0U;

    for (uint32_t i = 0U; i < MQS_NOTE_SAMPLES; i++)
    {
        int16_t s           = sine_sample(i % K_PERIOD_A4, K_PERIOD_A4);
        s_melody_buf[out++] = s; /* L */
        s_melody_buf[out++] = s; /* R == L */
    }

    for (uint32_t i = 0U; i < MQS_NOTE_SAMPLES; i++)
    {
        int16_t s           = sine_sample(i % K_PERIOD_E5, K_PERIOD_E5);
        s_melody_buf[out++] = s; /* L */
        s_melody_buf[out++] = s; /* R == L */
    }

    g_s_melody_ready = true;
}

/* --------------------------------------------------------------------------
 * Воспроизведение с USB keepalive
 * ----------------------------------------------------------------------- */

/*
 * Запускаем bsp_mqs_play() (не blocking), чтобы параллельно вызывать
 * bsp_usb_cdc_poll(). Без этого USB стек голодает за ~4 с воспроизведения
 * (keepalive-порог ~4 КБ переданных данных).
 */
static bsp_status_t mqs_play_with_poll(void)
{
    bsp_status_t status = bsp_mqs_play(s_melody_buf, MQS_TOTAL_FRAMES, NULL, NULL);
    if (status != BSP_OK)
    {
        return status;
    }

    while (bsp_mqs_is_busy())
    {
        bsp_usb_cdc_poll();
    }

    return BSP_OK;
}

/* --------------------------------------------------------------------------
 * test_module_t callbacks
 * ----------------------------------------------------------------------- */

static void test_mqs_init(void)
{
    mqs_build_melody();
    (void) bsp_mqs_init();
    (void) bsp_mqs_amp_init();
}

static test_result_t test_mqs_run(void)
{
    bsp_status_t play_st = mqs_play_with_poll();
    if (play_st != BSP_OK)
    {
        return (test_result_t){
            .status = TEST_STATUS_FAIL,
            .detail = "mqs play error",
        };
    }

    /* Запрашиваем подтверждение оператора.
     * API: bool test_runner_wait_confirm(const confirm_params_t *p_params)
     * Возвращает true если оператор подтвердил, false при отказе или таймауте.
     * Таймаут детектируется отдельно через флаг в confirm_params_t. */
    const confirm_params_t PARAMS = {
        .id         = "mqs_tone",
        .prompt     = "Have you heard a tone?",
        .timeout_ms = 15000U,
    };

    bool confirmed = test_runner_wait_confirm(&PARAMS);

    if (!confirmed)
    {
        /* Различаем таймаут и явный отказ через поле last_confirm_timed_out
         * если оно есть в API, иначе унифицируем как fail. */
        return (test_result_t){
            .status = TEST_STATUS_FAIL,
            .detail = "operator: no sound",
        };
    }

    return (test_result_t){ .status = TEST_STATUS_PASS, .detail = "" };
}

static void test_mqs_deinit(void)
{
    bsp_mqs_stop();
    bsp_mqs_amp_deinit();
    bsp_mqs_deinit();
}

/* --------------------------------------------------------------------------
 * Дескриптор модуля
 * ----------------------------------------------------------------------- */

const test_module_t K_TEST_MQS = {
    .id                 = "mqs",
    .name               = "MQS Audio Out",
    .critical           = false,
    .requires_hil       = false,
    .pre_confirm_prompt = NULL,
    .init               = test_mqs_init,
    .run                = test_mqs_run,
    .deinit             = test_mqs_deinit,
};