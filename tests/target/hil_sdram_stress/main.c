/*
 * hil_sdram_stress — HIL-прошивка стресс-теста SDRAM (PLAN.md, фаза 0.2).
 *
 * Запуск из RAM (код в ITCM, данные в DTCM/OCRAM): сама прошивка SDRAM не
 * использует, поэтому все 32 МБ — под тест, а чтение SDRAM не участвует в
 * выборке кода. SEMC поднимается bsp_sdram_run_dcd() из выбранного варианта
 * DCD (dcd/*.txt → C-массивы при сборке) — так каждый прогон может идти со
 * своим конфигом без пересборки DCD и HAB.
 *
 * Аппаратный сторож V3 кормится из хука SysTick: LED_HEARTBEAT (= LED_BLNK)
 * 250 мс горит / 250 мс нет (≥ 150 мс LOW — docs/hardware/new_board_v3.2/
 * HW_WATCHDOG.md). На старой плате это просто мигающий светодиод.
 * LED_APP (на V3 = SOUND_KEY) не трогаем — динамик отключён.
 *
 * Команды (UART 115200, строка \r\n → одна строка ответа, кроме RUN):
 *   PING                        → PONG
 *   BOOT                        → BOOT srsr=.. semc_clk=<0|1> [mcr=.. br0=..] sdramcr3=.. ren=<0|1>
 *                                 (ren=1 — SEMC уже кто-то поднял: не холодный старт;
 *                                 semc_clk=0 — тактирование SEMC выключено, регистры не читаются)
 *   VARIANTS                    → VARIANTS <имя>,<имя>,...
 *   INIT <вариант> [FORCE]      → INIT OK <регистры> | INIT ERR <причина>
 *                                 повторный INIT в той же загрузке — только с FORCE
 *   CACHE <ON|OFF>              → OK; ON — SDRAM Normal WB (burst-ы BL8 от кэша),
 *                                 OFF — Device (каждое обращение идёт в SDRAM). Старт: OFF
 *   RUN DATABUS                 ┐
 *   RUN ADDRBUS                 │ во время работы — «P» раз в ~0,5 с (keepalive),
 *   RUN MARCH <background-hex>  │ в конце — RESULT test=.. errors=.. first=0x.. exp=0x..
 *   RUN PRNG <seed>             │ act=0x.. diff=0x.. dq=0x.. ms=..
 *   RUN RETENTION <сек> <seed>  ┘ заполнить → ждать без обращений → проверить
 *   REGS                        → REGS <регистры SEMC/клоков>
 *   IN1                         → IN1 ad_b1_06=<0|1> emc_39=<0|1|->
 *   IN1 EDGES <мс>              → IN1 EDGES ad_b1_06=<n> emc_39=<n|-> ms=<мс>
 *                                 сверка проводки оптовхода EXT_IN1 (активный уровень — 1):
 *                                 старая плата — AD_B1_06 (GPIO1_IO22, pin_mux), V3 — EMC_39
 *                                 (GPIO3_IO25). EMC_39 читается, только если DCD варианта
 *                                 отдал его GPIO (ALT5, DQSMD=0), иначе «-»: пад занят SEMC_DQS.
 */

#include "board.h"
#include "fsl_clock.h"
#include "fsl_device_registers.h" /* SEMC/CCM/SRC/MPU — только чтение регистров и MPU */

#include "bsp/led.h"
#include "bsp/sdram.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"

#include "dcd_variants.h"
#include "mem_tests.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLI_BAUD_RATE   115200U
#define CLI_LINE_MAX    96U
#define READY_PERIOD_MS 200U
#define KEEPALIVE_MS    500U
#define WDT_HALF_MS     250U /* LED_BLNK: 250 LOW / 250 HIGH */

#define SDRAM_BASE  0x80000000UL
#define SDRAM_BYTES 0x02000000UL /* 32 МБ */
#define SDRAM_WORDS (SDRAM_BYTES / 4U)
#define MPU_REGION_SDRAM 8U /* как в board_mpu_init() */

/* CCGR3.CG2 — тактирование SEMC (kCLOCK_Semc). После BootROM при стёртой флеш
 * выключено, а чтение регистров периферии без тактирования вешает шину. */
#define CCGR3_SEMC_MASK 0x30UL

/* EXT_IN1: старая плата — AD_B1_06 = GPIO1_IO22 (мукс и вход — BOARD_InitPins()),
 * V3 — EMC_39 = GPIO3_IO25 (мукс — DCD варианта). */
#define IN1_LEGACY_PIN   22U
#define IN1_V3_PIN       25U
#define EMC_39_MUX_GPIO  5U /* ALT5 */
#define IN1_EDGES_MAX_MS 10000U

static volatile uint32_t *const P_SDRAM = (volatile uint32_t *) SDRAM_BASE;

static bool s_inited;
static bool s_cached;
static uint32_t s_last_keepalive_ms;
static char s_out[192];

/* ── Аппаратный сторож V3: кормление из SysTick ────────────────────────── */

void bsp_systick_hook(void)
{
    static uint32_t s_ms;
    if (++s_ms >= WDT_HALF_MS)
    {
        s_ms = 0U;
        bsp_led_toggle(LED_HEARTBEAT);
    }
}

/* ── Вывод ─────────────────────────────────────────────────────────────── */

static void out(const char *p_str)
{
    bsp_uart_host_write_str(p_str);
}

static void keepalive(void)
{
    const uint32_t NOW = bsp_tick_get_ms();
    if ((NOW - s_last_keepalive_ms) >= KEEPALIVE_MS)
    {
        s_last_keepalive_ms = NOW;
        out("P\r\n");
    }
}

/* ── Кэш SDRAM ─────────────────────────────────────────────────────────── */

static void cache_sync(void)
{
    SCB_CleanInvalidateDCache();
}

static void sdram_cache_set(bool on)
{
    SCB_CleanInvalidateDCache();
    ARM_MPU_Disable();
    if (on)
    {
        MPU->RBAR = ARM_MPU_RBAR(MPU_REGION_SDRAM, SDRAM_BASE);
        MPU->RASR = ARM_MPU_RASR(0U, ARM_MPU_AP_FULL, 0U, 0U, 1U, 1U, 0U, ARM_MPU_REGION_SIZE_32MB);
    }
    else
    {
        ARM_MPU_ClrRegion(MPU_REGION_SDRAM); /* остаётся Region 1: Device */
    }
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
    __DSB();
    __ISB();
    s_cached = on;
}

static const mem_hooks_t *hooks(void)
{
    static mem_hooks_t s_hooks;
    s_hooks.sync     = s_cached ? cache_sync : NULL;
    s_hooks.progress = keepalive;
    return &s_hooks;
}

/* ── Регистры ──────────────────────────────────────────────────────────── */

static bool semc_clocked(void)
{
    return (CCM->CCGR3 & CCGR3_SEMC_MASK) != 0U;
}

static uint32_t semc_khz(void)
{
    const uint32_t CBCDR = CCM->CBCDR;
    const uint32_t PODF  = ((CBCDR >> 16) & 0x7U) + 1U;
    if ((CBCDR & 0x40U) == 0U)
    {
        return 0U; /* SEMC от periph_clk — не наш источник, не считаем */
    }
    /* SEMC_ALT_CLK_SEL: 0 — PLL2 PFD2 (528 МГц), 1 — PLL3 PFD1 (480 МГц) */
    const bool PLL3     = (CBCDR & 0x80U) != 0U;
    const uint32_t FRAC = PLL3 ? ((CCM_ANALOG->PFD_480 >> 8) & 0x3FU) : ((CCM_ANALOG->PFD_528 >> 16) & 0x3FU);
    if ((FRAC < 12U) || (FRAC > 35U))
    {
        return 0U; /* вне допустимого по RM — PFD не даёт такта */
    }
    return ((PLL3 ? 480000U : 528000U) * 18U / FRAC) / PODF;
}

static void print_regs(const char *p_prefix)
{
    if (!semc_clocked())
    {
        (void) snprintf(s_out, sizeof(s_out), "%s semc_clk=0\r\n", p_prefix);
        out(s_out);
        return;
    }
    (void) snprintf(s_out, sizeof(s_out),
                    "%s mcr=0x%08lX cr0=0x%08lX cr1=0x%08lX cr2=0x%08lX cr3=0x%08lX "
                    "pfd528=0x%08lX pfd480=0x%08lX cbcdr=0x%08lX semc_khz=%lu\r\n",
                    p_prefix, (unsigned long) SEMC->MCR, (unsigned long) SEMC->SDRAMCR0,
                    (unsigned long) SEMC->SDRAMCR1, (unsigned long) SEMC->SDRAMCR2,
                    (unsigned long) SEMC->SDRAMCR3, (unsigned long) CCM_ANALOG->PFD_528,
                    (unsigned long) CCM_ANALOG->PFD_480, (unsigned long) CCM->CBCDR, (unsigned long) semc_khz());
    out(s_out);
}

/* ── Команды ───────────────────────────────────────────────────────────── */

/**
 * Следующее слово строки (разделитель — пробел), на месте, без кучи.
 * strtok() из newlib-nano при первом вызове выделяет служебную структуру
 * из кучи (_REENT_CHECK_MISC), а у RAM-прошивки её нет → abort() → _exit.
 */
static char *next_token(char **pp_cursor)
{
    char *p = *pp_cursor;
    while (*p == ' ')
    {
        p++;
    }
    if (*p == '\0')
    {
        *pp_cursor = p;
        return NULL;
    }
    char *const P_START = p;
    while ((*p != ' ') && (*p != '\0'))
    {
        p++;
    }
    if (*p == ' ')
    {
        *p++ = '\0';
    }
    *pp_cursor = p;
    return P_START;
}

static void cmd_boot(void)
{
    if (!semc_clocked())
    {
        /* Никто не включал SEMC — холодный старт; регистры не читаем (зависание шины). */
        (void) snprintf(s_out, sizeof(s_out), "BOOT srsr=0x%03lX semc_clk=0 sdramcr3=- ren=0\r\n",
                        (unsigned long) SRC->SRSR);
        out(s_out);
        return;
    }
    (void) snprintf(s_out, sizeof(s_out),
                    "BOOT srsr=0x%03lX semc_clk=1 mcr=0x%08lX br0=0x%08lX sdramcr3=0x%08lX ren=%u\r\n",
                    (unsigned long) SRC->SRSR, (unsigned long) SEMC->MCR,
                    (unsigned long) SEMC->BR[0], (unsigned long) SEMC->SDRAMCR3,
                    (SEMC->SDRAMCR3 & 1U) ? 1U : 0U);
    out(s_out);
}

static void cmd_variants(void)
{
    out("VARIANTS ");
    for (size_t i = 0U; i < g_dcd_variant_count; i++)
    {
        out(g_dcd_variants[i].name);
        out((i + 1U < g_dcd_variant_count) ? "," : "\r\n");
    }
}

static void cmd_init(char *p_args)
{
    char *p_cursor            = p_args;
    const char *const P_NAME  = next_token(&p_cursor);
    const char *const P_FORCE = next_token(&p_cursor);
    const dcd_variant_t *p_var = NULL;

    for (size_t i = 0U; (P_NAME != NULL) && (i < g_dcd_variant_count); i++)
    {
        if (strcmp(P_NAME, g_dcd_variants[i].name) == 0)
        {
            p_var = &g_dcd_variants[i];
        }
    }
    if (p_var == NULL)
    {
        out("INIT ERR unknown_variant\r\n");
        return;
    }
    if (s_inited && ((P_FORCE == NULL) || (strcmp(P_FORCE, "FORCE") != 0)))
    {
        out("INIT ERR already_inited\r\n");
        return;
    }

    const bsp_status_t STATUS = bsp_sdram_run_dcd(p_var->data, *p_var->p_size);
    if (STATUS != BSP_OK)
    {
        (void) snprintf(s_out, sizeof(s_out), "INIT ERR status=%d\r\n", (int) STATUS);
        out(s_out);
        return;
    }
    s_inited = true;
    print_regs("INIT OK");
}

static void print_result(const char *p_test, const mem_result_t *p_res, uint32_t ms)
{
    (void) snprintf(s_out, sizeof(s_out),
                    "RESULT test=%s cache=%u errors=%lu first=0x%08lX exp=0x%08lX act=0x%08lX "
                    "diff=0x%08lX dq=0x%04X ms=%lu\r\n",
                    p_test, s_cached ? 1U : 0U, (unsigned long) p_res->errors,
                    (unsigned long) p_res->first_offset, (unsigned long) p_res->expected,
                    (unsigned long) p_res->actual, (unsigned long) p_res->diff_or,
                    (unsigned) mem_dq_mask(p_res), (unsigned long) ms);
    out(s_out);
}

static void wait_seconds(uint32_t seconds)
{
    const uint32_t START = bsp_tick_get_ms();
    while ((bsp_tick_get_ms() - START) < (seconds * 1000U))
    {
        keepalive(); /* SDRAM не трогаем — работает только refresh контроллера */
    }
}

static void cmd_run(char *p_args)
{
    char *p_cursor           = p_args;
    const char *const P_TEST = next_token(&p_cursor);
    const char *const P_A1   = next_token(&p_cursor);
    const char *const P_A2   = next_token(&p_cursor);
    mem_result_t res;

    if (!s_inited)
    {
        out("RESULT ERR not_inited\r\n");
        return;
    }
    if (P_TEST == NULL)
    {
        out("RESULT ERR no_test\r\n");
        return;
    }

    mem_result_clear(&res);
    s_last_keepalive_ms = bsp_tick_get_ms();
    const uint32_t START = s_last_keepalive_ms;

    if (strcmp(P_TEST, "DATABUS") == 0)
    {
        mem_test_databus(P_SDRAM, &res);
    }
    else if (strcmp(P_TEST, "ADDRBUS") == 0)
    {
        mem_test_addrbus(P_SDRAM, SDRAM_WORDS, hooks(), &res);
    }
    else if (strcmp(P_TEST, "MARCH") == 0)
    {
        const uint32_t BG = (P_A1 != NULL) ? (uint32_t) strtoul(P_A1, NULL, 16) : 0U;
        mem_test_march_c(P_SDRAM, SDRAM_WORDS, BG, hooks(), &res);
    }
    else if (strcmp(P_TEST, "PRNG") == 0)
    {
        const uint32_t SEED = (P_A1 != NULL) ? (uint32_t) strtoul(P_A1, NULL, 0) : 1U;
        mem_prng_fill(P_SDRAM, SDRAM_WORDS, SEED, hooks());
        mem_prng_verify(P_SDRAM, SDRAM_WORDS, SEED, hooks(), &res);
    }
    else if (strcmp(P_TEST, "RETENTION") == 0)
    {
        const uint32_t SECONDS = (P_A1 != NULL) ? (uint32_t) strtoul(P_A1, NULL, 0) : 1U;
        const uint32_t SEED    = (P_A2 != NULL) ? (uint32_t) strtoul(P_A2, NULL, 0) : 1U;
        mem_prng_fill(P_SDRAM, SDRAM_WORDS, SEED, hooks()); /* fill делает sync в конце */
        wait_seconds(SECONDS);
        mem_prng_verify(P_SDRAM, SDRAM_WORDS, SEED, hooks(), &res);
    }
    else
    {
        out("RESULT ERR unknown_test\r\n");
        return;
    }
    print_result(P_TEST, &res, bsp_tick_get_ms() - START);
}

/* ── EXT_IN1: сверка проводки ──────────────────────────────────────────── */

static uint32_t in1_legacy(void)
{
    return (GPIO1->PSR >> IN1_LEGACY_PIN) & 1U;
}

/** EMC_39 как GPIO: 0/1, или -1, если пад отдан SEMC_DQS. */
static int32_t in1_v3(void)
{
    if ((IOMUXC->SW_MUX_CTL_PAD[kIOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_39] & 0x7U) != EMC_39_MUX_GPIO)
    {
        return -1;
    }
    CLOCK_EnableClock(kCLOCK_Gpio3);
    GPIO3->GDIR &= ~(1UL << IN1_V3_PIN); /* вход (после сброса и так) */
    return (int32_t) ((GPIO3->PSR >> IN1_V3_PIN) & 1U);
}

static void cmd_in1(char *p_args)
{
    char *p_cursor          = p_args;
    const char *const P_SUB = next_token(&p_cursor);
    const char *const P_MS  = next_token(&p_cursor);

    if (P_SUB == NULL)
    {
        const int32_t V3 = in1_v3();
        if (V3 < 0)
        {
            (void) snprintf(s_out, sizeof(s_out), "IN1 ad_b1_06=%lu emc_39=-\r\n",
                            (unsigned long) in1_legacy());
        }
        else
        {
            (void) snprintf(s_out, sizeof(s_out), "IN1 ad_b1_06=%lu emc_39=%ld\r\n",
                            (unsigned long) in1_legacy(), (long) V3);
        }
        out(s_out);
        return;
    }
    if ((strcmp(P_SUB, "EDGES") != 0) || (P_MS == NULL))
    {
        out("IN1 ERR usage\r\n");
        return;
    }
    uint32_t ms = (uint32_t) strtoul(P_MS, NULL, 0);
    if (ms > IN1_EDGES_MAX_MS)
    {
        ms = IN1_EDGES_MAX_MS;
    }

    /* Опрос в цикле: меандр M5 — единицы Гц, дребезг реле тоже посчитается,
     * поэтому хост проверяет «не меньше ожидаемого», а не точное число. */
    uint32_t last_legacy = in1_legacy();
    int32_t last_v3      = in1_v3();
    uint32_t edges_legacy = 0U;
    uint32_t edges_v3     = 0U;
    const uint32_t START  = bsp_tick_get_ms();
    while ((bsp_tick_get_ms() - START) < ms)
    {
        const uint32_t LEGACY = in1_legacy();
        edges_legacy += (LEGACY != last_legacy) ? 1U : 0U;
        last_legacy = LEGACY;
        if (last_v3 >= 0)
        {
            const int32_t V3 = in1_v3();
            edges_v3 += (V3 != last_v3) ? 1U : 0U;
            last_v3 = V3;
        }
    }
    if (last_v3 < 0)
    {
        (void) snprintf(s_out, sizeof(s_out), "IN1 EDGES ad_b1_06=%lu emc_39=- ms=%lu\r\n",
                        (unsigned long) edges_legacy, (unsigned long) ms);
    }
    else
    {
        (void) snprintf(s_out, sizeof(s_out), "IN1 EDGES ad_b1_06=%lu emc_39=%lu ms=%lu\r\n",
                        (unsigned long) edges_legacy, (unsigned long) edges_v3, (unsigned long) ms);
    }
    out(s_out);
}

static void cli_process(char *p_line)
{
    if (strcmp(p_line, "PING") == 0)
    {
        out("PONG\r\n");
    }
    else if (strcmp(p_line, "BOOT") == 0)
    {
        cmd_boot();
    }
    else if (strcmp(p_line, "VARIANTS") == 0)
    {
        cmd_variants();
    }
    else if (strncmp(p_line, "INIT ", 5U) == 0)
    {
        cmd_init(&p_line[5]);
    }
    else if (strcmp(p_line, "CACHE ON") == 0)
    {
        sdram_cache_set(true);
        out("OK\r\n");
    }
    else if (strcmp(p_line, "CACHE OFF") == 0)
    {
        sdram_cache_set(false);
        out("OK\r\n");
    }
    else if (strncmp(p_line, "RUN ", 4U) == 0)
    {
        cmd_run(&p_line[4]);
    }
    else if (strcmp(p_line, "REGS") == 0)
    {
        print_regs("REGS");
    }
    else if ((strcmp(p_line, "IN1") == 0) || (strncmp(p_line, "IN1 ", 4U) == 0))
    {
        cmd_in1(&p_line[3]);
    }
    else if (p_line[0] != '\0')
    {
        out("ERR_UNKNOWN\r\n");
    }
    else
    {
        /* пустая строка */
    }
}

/** Неблокирующий сбор строки. */
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
    sdram_cache_set(false);

    static char s_line[CLI_LINE_MAX];
    uint32_t last_ready_ms = 0U;
    bool host_seen         = false;

    for (;;)
    {
        const uint32_t NOW = bsp_tick_get_ms();
        if (!host_seen && ((NOW - last_ready_ms) >= READY_PERIOD_MS))
        {
            last_ready_ms = NOW;
            out("READY\r\n");
        }
        if (cli_poll_line(s_line, sizeof(s_line)))
        {
            host_seen = true;
            cli_process(s_line);
        }
    }
}
