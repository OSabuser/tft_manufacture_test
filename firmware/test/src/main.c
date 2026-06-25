/**
 * @file  main_mqs_test.c
 * @brief Изолированный тест bsp_mqs + bsp_mqs_amp.
 *
 * Подменяет firmware/test/src/main.c на время отладки BSP.
 * Вывод диагностики — UART (MCU-Link VCOM, 115200 бод).
 *
 * Два режима — переключаются комментарием в main():
 *   TEST_MODE_POLLING — SAI_WriteBlocking напрямую, без DMA.
 *                       Если звук есть → SAI/MQS OK, проблема в DMA.
 *                       Если нет       → проблема в SAI/MQS/усилителе.
 *   TEST_MODE_DMA     — bsp_mqs_play_blocking через eDMA.
 */

#include "board.h"
#include "bsp/led.h"
#include "bsp/mqs.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "fsl_sai.h"

#include <stdint.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Выбор режима: раскомментируй один
 * ----------------------------------------------------------------------- */
//#define TEST_MODE_POLLING
#define TEST_MODE_DMA

/* --------------------------------------------------------------------------
 * Буфер мелодии в некэшируемой секции
 * ----------------------------------------------------------------------- */

#define MQS_NOTE_SAMPLES (44100U * 2U)           /* 2 с на ноту             */
#define MQS_TOTAL_FRAMES (MQS_NOTE_SAMPLES * 2U) /* 2 ноты                 */
#define MQS_BUF_ELEMENTS (MQS_TOTAL_FRAMES * BSP_MQS_CHANNELS)

static AT_NONCACHEABLE_SECTION_ALIGN(int16_t s_melody_buf[MQS_BUF_ELEMENTS], 4U);

/* --------------------------------------------------------------------------
 * Синусоида (целочисленная, LUT по четвертям)
 * ----------------------------------------------------------------------- */

static int16_t sine_sample(uint32_t phase, uint32_t period)
{
    static const int16_t k_lut[25] = {
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
        val = k_lut[pos];
        break;
    case 1U:
        val = k_lut[24U - pos];
        break;
    case 2U:
        val = -k_lut[pos];
        break;
    default:
        val = -k_lut[24U - pos];
        break;
    }
    return val;
}

static void build_melody(void)
{
    const uint32_t k_p_a4 = 100U; /* A4 ≈ 440 Гц */
    const uint32_t k_p_e5 = 67U;  /* E5 ≈ 659 Гц */
    uint32_t out          = 0U;

    for (uint32_t i = 0U; i < MQS_NOTE_SAMPLES; i++)
    {
        int16_t s           = sine_sample(i % k_p_a4, k_p_a4);
        s_melody_buf[out++] = s; /* L */
        s_melody_buf[out++] = s; /* R */
    }
    for (uint32_t i = 0U; i < MQS_NOTE_SAMPLES; i++)
    {
        int16_t s           = sine_sample(i % k_p_e5, k_p_e5);
        s_melody_buf[out++] = s; /* L */
        s_melody_buf[out++] = s; /* R */
    }
}

/* --------------------------------------------------------------------------
 * Режим A: SAI polling напрямую, без DMA
 *
 * SAI_WriteBlocking — блокирует до записи всех байт через FIFO.
 * Не требует DMA вообще. Если звук есть — SAI и MQS живые.
 * ----------------------------------------------------------------------- */

#ifdef TEST_MODE_POLLING
static void run_test(void)
{
    bsp_uart_host_write_str("[5A] Bare-metal SAI polling...\r\n");

    /* 1. Глушим передатчик и отключаем запросы DMA, чтобы не мешали поллингу */
    SAI3->TCSR &= ~(I2S_TCSR_TE_MASK | I2S_TCSR_BCE_MASK | I2S_TCSR_FRDE_MASK);

    /* 2. Сбрасываем ошибку Underrun (FEF) и чистим FIFO (FR) */
    SAI3->TCSR |= I2S_TCSR_FEF_MASK | I2S_TCSR_FR_MASK;

    /* 3. Предзагружаем FIFO (закинем 16 сэмплов ДО включения) */
    /* Это спасет аппаратуру от мгновенного Underrun'а при старте */
    for (uint32_t i = 0; i < 16; i++)
    {
        SAI3->TDR[0] = s_melody_buf[i];
    }

    /* 4. Включаем передатчик и генерацию Bit Clock */
    SAI3->TCSR |= (I2S_TCSR_TE_MASK | I2S_TCSR_BCE_MASK);

    /* 5. Качаем остальной буфер вручную */
    for (uint32_t i = 16; i < MQS_BUF_ELEMENTS; i++)
    {
        /* Ждем, пока в FIFO появится свободное место (FWF == 1) */
        while ((SAI3->TCSR & I2S_TCSR_FWF_MASK) == 0)
        {
            /* Если всё-таки поймали опустошение - перезапускаем TX на лету */
            if (SAI3->TCSR & I2S_TCSR_FEF_MASK)
            {
                SAI3->TCSR |= I2S_TCSR_FEF_MASK;                    /* Сброс ошибки */
                SAI3->TCSR |= I2S_TCSR_TE_MASK | I2S_TCSR_BCE_MASK; /* Рестарт TX */
            }
        }
        /* Пишем следующий сэмпл */
        SAI3->TDR[0] = s_melody_buf[i];
    }

    /* 6. Дожидаемся, пока уйдут остатки из FIFO, и выключаем */
    while (SAI3->TCSR & I2S_TCSR_FWF_MASK)
    {
    }
    SAI3->TCSR &= ~(I2S_TCSR_TE_MASK | I2S_TCSR_BCE_MASK);

    bsp_uart_host_write_str("[5A] Done.\r\n");
}
#endif

/* --------------------------------------------------------------------------
 * Режим B: DMA через bsp_mqs_play_blocking
 * ----------------------------------------------------------------------- */

#ifdef TEST_MODE_DMA
static void run_test(void)
{
    bsp_uart_host_write_str("[5B] DMA play, ~4s...\r\n");

    bsp_status_t st = bsp_mqs_play_blocking(s_melody_buf, MQS_TOTAL_FRAMES);

    bsp_uart_host_write_str(st == BSP_OK ? "[5B] OK\r\n" : "[5B] FAIL\r\n");
}
#endif

/* --------------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    (void) bsp_uart_host_init(115200U);

    bsp_led_on(LED_HEARTBEAT);
    bsp_uart_host_write_str("\r\n=== MQS BSP TEST ===\r\n");

    /* Шаг 1 — усилитель */
    bsp_uart_host_write_str("[1] amp_init...\r\n");
    bsp_status_t st = bsp_mqs_amp_init();
    bsp_uart_host_write_str(st == BSP_OK ? "[1] OK\r\n" : "[1] FAIL\r\n");

    /* Шаг 2 — SAI + DMA + MQS */
    bsp_uart_host_write_str("[2] mqs_init...\r\n");
    st = bsp_mqs_init();
    bsp_uart_host_write_str(st == BSP_OK ? "[2] OK\r\n" : "[2] FAIL\r\n");

    /* Шаг 3 — генерация буфера */
    bsp_uart_host_write_str("[3] build_melody...\r\n");
    build_melody();
    bsp_uart_host_write_str("[3] done.\r\n");

    /* Основной цикл */
    for (;;)
    {
        bsp_led_toggle(LED_HEARTBEAT);
        run_test();
        bsp_uart_host_write_str("[pause] 3s\r\n");
        bsp_delay(3000U);
    }

    return 0;
}
#if 0
/**
 * @file  main.c
 * @brief firmware_test — точка входа.
 *
 * Bare-metal входной контроль платы.
 * Единственный канал хост↔таргет: USB CDC ACM (J2).
 * Протокол: JSON-lines через cli.c.
 *
 * Последовательность старта:
 *   1. board_hw_init()    — тактирование, MPU, кэш, пины
 *   2. bsp_tick_init()    — SysTick 1 мс
 *   3. bsp_led_init()     — оба LED выключены
 *   4. bsp_usb_cdc_init() — PHY + стек + NVIC
 *   5. Ожидание CDC ready — LED_HEARTBEAT мигает
 *   6. cli_init()         — сброс буфера
 *   7. READY → хост       — JSON сигнал готовности
 *   8. Главный цикл       — poll + cli_process
 */
#include "board.h"
#include "bsp/can.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "cli.h"
#include "protocol.h"
#include "test_runner.h"

#include <stdint.h>

int main(void)
{
    const uint32_t CONNECT_BLINK_MS         = 200U;
    const uint32_t ERROR_BLINK_MS           = 250;
    static const bsp_can_config_t K_CAN_CFG = { .bitrate = 125000U };

    board_hw_init();

    bsp_led_init();
    bsp_tick_init();

    if (bsp_usb_cdc_init() != BSP_OK)
    {
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(ERROR_BLINK_MS);
    }

    /* Ожидать подключения хоста. LED_HEARTBEAT мигает — прошивка жива. */
    while (!bsp_usb_cdc_is_ready())
    {
        bsp_usb_cdc_poll();
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(CONNECT_BLINK_MS);
    }

    bsp_led_on(LED_APP);

    cli_init();
    test_runner_init();
    protocol_send_session_start();
    while (1)
    {
        bsp_usb_cdc_poll();
        cli_process();
        test_runner_process();
    }
}
#endif