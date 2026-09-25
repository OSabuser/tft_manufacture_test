/*
 * hil_hw_wdt — HIL-прошивка для измерения аппаратного watchdog платы V3.x.
 *
 * Сторож (лист 2 схемы МЮ.Д08.06.10-01): LED_BLNK (= Led_1 = LED_HEARTBEAT,
 * GPIO_SD_B1_03) → C73 → VT8A → RC-накопитель → компаратор D8 → VT8B → VT7
 * тянет EN основного DC/DC BL9362 → передёргивание питания всей платы.
 * Работает только при питании от VIN (4V9 компаратора и BL9362 — от VIN).
 *
 * Прошивка управляет LED_BLNK по команде и непрерывно шлёт метку времени,
 * по пропаданию потока хост меряет таймаут сторожа.
 *
 * Причина сброса — SRC_SRSR: прошивка читает его при старте и ЧИСТИТ (w1c).
 * Поэтому при следующей загрузке SRSR показывает только то, что случилось
 * с чипом между загрузками: 0 — сброса не было, IPP_RESET_B (bit0) — POR,
 * т.е. пропадало питание, WDOG_RST_B/WDOG3_RST_B — внутренние сторожа и т.д.
 * Условие: прошитый во флеш образ не должен сам чистить SRSR (bootloader это
 * делает в bsp_boot_state_init()) — проверяется контрольными тестами хоста.
 * (Маркер в SNVS_LPGPR0 не годится: на этой плате он не держится даже без
 * сброса — первый прогон 2026-09-24.)
 *
 * Дополнительно BOOT сообщает, взведены ли внутренние сторожа (WDOG1/2,
 * RTWDOG): если все выключены, сброс по ним исключён.
 *
 * Команды (UART, 115200, строки \r\n):
 *   PING                     → PONG
 *   BOOT                     → BOOT srsr=0x........ wdog1=<0|1> wdog2=<0|1>
 *                                   rtwdog=<0|1> uptime=<мс>
 *   FEED <period_ms> <on_ms> → OK; LED горит on_ms в каждом периоде
 *   HOLD <ON|OFF>            → OK; перестать кормить, держать уровень
 *                              (ON — LED горит, пин LOW; OFF — пин HIGH)
 *   После FEED/HOLD каждые STREAM_PERIOD_MS: T <мс с момента команды>
 *
 * LED_APP (Led_2) на V3 = SOUND_KEY — не трогаем: после bsp_led_init() он
 * выключен (пин HIGH → динамик отключён).
 */

#include "board.h"
#include "fsl_device_registers.h" /* SRC/WDOG — только диагностика сброса */
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLI_BAUD_RATE    115200U
#define CLI_LINE_MAX     64U
#define READY_PERIOD_MS  200U
#define STREAM_PERIOD_MS 10U

/* Режим после старта — кормим, чтобы плата пережила ожидание хоста. */
#define DEFAULT_PERIOD_MS 200U
#define DEFAULT_ON_MS     100U

/* SRC_SRSR: все w1c-биты причин сброса (RM 21.8.3). */
#define SRSR_ALL_CAUSES 0x000001FFUL

typedef enum wdt_mode_e
{
    MODE_FEED,
    MODE_HOLD,
} wdt_mode_t;

typedef struct wdt_state_s
{
    wdt_mode_t mode;
    uint32_t period_ms;
    uint32_t on_ms;
    bool hold_on;
    uint32_t t0_ms;
    bool streaming;
    uint32_t last_stream_ms;
} wdt_state_t;

static wdt_state_t s_st = {
    .mode = MODE_FEED, .period_ms = DEFAULT_PERIOD_MS, .on_ms = DEFAULT_ON_MS,
};

typedef struct boot_diag_s
{
    uint32_t srsr;
    bool wdog1_on;
    bool wdog2_on;
    bool rtwdog_on;
} boot_diag_t;

static boot_diag_t s_boot;

/* ── Причина сброса ──────────────────────────────────────────────────── */

static void boot_diag_capture(void)
{
    s_boot.srsr      = SRC->SRSR;
    SRC->SRSR        = SRSR_ALL_CAUSES; /* w1c: следующая загрузка увидит только новое */
    s_boot.wdog1_on  = (WDOG1->WCR & WDOG_WCR_WDE_MASK) != 0U;
    s_boot.wdog2_on  = (WDOG2->WCR & WDOG_WCR_WDE_MASK) != 0U;
    s_boot.rtwdog_on = (RTWDOG->CS & RTWDOG_CS_EN_MASK) != 0U;
}

/* ── Вывод ───────────────────────────────────────────────────────────── */

static void reply(const char *p_fmt, uint32_t a, uint32_t b, uint32_t c)
{
    static char s_buf[96];
    (void) snprintf(s_buf, sizeof(s_buf), p_fmt, (unsigned long) a, (unsigned long) b,
                    (unsigned long) c);
    bsp_uart_host_write_str(s_buf);
}

/* ── Режимы ──────────────────────────────────────────────────────────── */

static void mode_enter(wdt_mode_t mode)
{
    s_st.mode           = mode;
    s_st.t0_ms          = bsp_tick_get_ms();
    s_st.streaming      = true;
    s_st.last_stream_ms = s_st.t0_ms;
}

static void led_update(uint32_t now)
{
    if (s_st.mode == MODE_HOLD)
    {
        bsp_led_set(LED_HEARTBEAT, s_st.hold_on);
        return;
    }
    const uint32_t PHASE = (now - s_st.t0_ms) % s_st.period_ms;
    bsp_led_set(LED_HEARTBEAT, PHASE < s_st.on_ms);
}

static void stream_update(uint32_t now)
{
    if (s_st.streaming && ((now - s_st.last_stream_ms) >= STREAM_PERIOD_MS))
    {
        s_st.last_stream_ms = now;
        reply("T %lu\r\n", now - s_st.t0_ms, 0U, 0U);
    }
}

/* ── CLI ─────────────────────────────────────────────────────────────── */

static void cmd_feed(const char *p_args)
{
    char *p_end            = NULL;
    const uint32_t PERIOD  = (uint32_t) strtoul(p_args, &p_end, 10);
    const uint32_t ON      = (uint32_t) strtoul(p_end, NULL, 10);

    if ((PERIOD < 2U) || (ON == 0U) || (ON >= PERIOD))
    {
        bsp_uart_host_write_str("ERR_ARG\r\n");
        return;
    }
    s_st.period_ms = PERIOD;
    s_st.on_ms     = ON;
    mode_enter(MODE_FEED);
    bsp_uart_host_write_str("OK\r\n");
}

static void cmd_hold(const char *p_args)
{
    if ((strcmp(p_args, "ON") != 0) && (strcmp(p_args, "OFF") != 0))
    {
        bsp_uart_host_write_str("ERR_ARG\r\n");
        return;
    }
    s_st.hold_on = (strcmp(p_args, "ON") == 0);
    mode_enter(MODE_HOLD);
    led_update(bsp_tick_get_ms()); /* уровень — сразу, до ответа */
    bsp_uart_host_write_str("OK\r\n");
}

static void cli_process(const char *p_line)
{
    if (strcmp(p_line, "PING") == 0)
    {
        bsp_uart_host_write_str("PONG\r\n");
    }
    else if (strcmp(p_line, "BOOT") == 0)
    {
        static char s_buf[112];
        (void) snprintf(s_buf, sizeof(s_buf),
                        "BOOT srsr=0x%08lX wdog1=%u wdog2=%u rtwdog=%u uptime=%lu\r\n",
                        (unsigned long) s_boot.srsr, s_boot.wdog1_on ? 1U : 0U,
                        s_boot.wdog2_on ? 1U : 0U, s_boot.rtwdog_on ? 1U : 0U,
                        (unsigned long) bsp_tick_get_ms());
        bsp_uart_host_write_str(s_buf);
    }
    else if (strncmp(p_line, "FEED ", 5U) == 0)
    {
        cmd_feed(&p_line[5]);
    }
    else if (strncmp(p_line, "HOLD ", 5U) == 0)
    {
        cmd_hold(&p_line[5]);
    }
    else if (p_line[0] != '\0')
    {
        bsp_uart_host_write_str("ERR_UNKNOWN\r\n");
    }
    else
    {
        /* пустая строка */
    }
}

/** Неблокирующий сбор строки: кормление LED не должно ждать UART. */
static bool cli_poll_line(char *p_line, size_t max_len)
{
    static size_t s_pos;

    while (bsp_uart_host_rx_available() > 0U)
    {
        const int32_t BYTE = bsp_uart_host_read_byte(0U);
        if ((BYTE < 0) || (BYTE == '\r'))
        {
            continue;
        }
        if (BYTE == '\n')
        {
            p_line[s_pos] = '\0';
            s_pos         = 0U;
            return true;
        }
        if (s_pos < (max_len - 1U))
        {
            p_line[s_pos++] = (char) BYTE;
        }
    }
    return false;
}

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    bsp_uart_host_init(CLI_BAUD_RATE);
    boot_diag_capture();

    s_st.t0_ms = bsp_tick_get_ms();

    static char s_line[CLI_LINE_MAX];
    uint32_t last_ready_ms = 0U;
    bool host_seen         = false;

    for (;;)
    {
        const uint32_t NOW = bsp_tick_get_ms();

        led_update(NOW);

        if (!host_seen && ((NOW - last_ready_ms) >= READY_PERIOD_MS))
        {
            last_ready_ms = NOW;
            bsp_uart_host_write_str("READY\r\n");
        }

        if (cli_poll_line(s_line, sizeof(s_line)))
        {
            host_seen = true;
            cli_process(s_line);
        }

        stream_update(NOW);
    }
}
