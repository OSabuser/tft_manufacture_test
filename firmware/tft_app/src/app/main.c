/**
 * @file  main.c
 * @brief tft_app — точка входа.
 *
 * Фаза 0 (каркас, подтверждено на железе): образ линкуется как валидный
 * MCUboot-слот (Direct-XIP), bootloader в него прыгает, FreeRTOS стартует,
 * WDOG кормится, образ ПОДТВЕРЖДАЕТ СЕБЯ в рантайме (нет revert на повторной
 * загрузке — anti-brick сохранён). Контракт с bootloader и обоснование
 * self-confirm/QSPI-ramfunc/UART-диагностики — см. PLAN.md, Фаза 0.
 *
 * Фаза 1 (walking skeleton): реальный CAN-кадр НКУ-CAN → чистый декодер →
 * контроллер → fallback-рендер (позиция + стрелка). Две задачи:
 *   - sul_rx_task — WDOG/heartbeat (унаследовано от Фазы 0) + приём CAN +
 *     decode + controller_process(); на изменение — в очередь render_task.
 *   - render_task — потребляет очередь, зовёт ui_fallback_render().
 * Очередь глубиной 1 с xQueueOverwrite() — важно только ПОСЛЕДНЕЕ состояние,
 * не история промежуточных кадров (render не обязан успевать за каждым).
 */

#include "FreeRTOS.h"
#include "board.h"
#include "bootutil/bootutil_public.h"
#include "bsp/boot_state.h"
#include "bsp/display.h"
#include "bsp/led.h"
#include "bsp/qspi_flash.h"
#include "bsp/sdram.h"
#include "bsp/uart_host.h"
#include "bsp/wdog.h"
#include "domain/controller.h"
#include "domain/elevator_model.h"
#include "domain/sul.h"
#include "domain/sul/nku_can.h"
#include "domain/sul/transport/can.h"
#include "flash_map.h"
#include "log/log.h"
#include "port/log_uart.h"
#include "queue.h"
#include "services/gfx.h"
#include "services/settings_store.h"
#include "task.h"
#include "ui/fallback.h"

#include <stdbool.h>

#define LOG_TAG "app"

/* Собственный слот образа. Slot A = 0 (primary), Slot Б = 1 (secondary). */
#ifndef APP_OWN_SLOT_ID
#define APP_OWN_SLOT_ID 0
#endif

/* Панель текущего стенда — Фаза 1 хардкод (Фаза 9: provisioning выбирает из
 * settings, см. ARCH §9 — TFT7/8/10 рантайм-выбор внутри профиля app-big). */
#define PANEL_TYPE BSP_DISPLAY_TFT8

#define HEARTBEAT_PERIOD_MS 500U
#define WDOG_FEED_PERIOD_MS 100U /* кормим чаще периода мигания — таймаут WDOG >= 1 c */
#define STATUS_LOG_PERIOD_MS                                                                       \
    2000U /* периодический re-log трейлера — виден независимо
                                     * от момента подключения терминала */
#define CAN_RX_TIMEOUT_MS 100U /* держит цикл отзывчивым к WDOG/heartbeat-каденции */
#define CONNECTION_TIMEOUT_MS                                                                      \
    3000U /* «пропадание трафика» — см. ARCH, поток данных:
                                     * poll + timeout→default. Порядок величины — как
                                     * в OLD_PROJECT (там ~3 с на отметку потери связи) */

/* TEMP (Фаза 3.1): двухзагрузочная HW-проверка пути save/персист. УБРАТЬ в 3.2,
 * когда save начнёт вызываться из меню. Сентинел в ИНЕРТНОМ поле max_load_kg
 * (рендер грузоподъёмности — Фаза 5), proto_slice/адрес НЕ трогаем — индикатор
 * продолжает работать с адресом 0. */
#define SETTINGS_SELFTEST_SENTINEL 4242U

/* Передача render_task самого свежего состояния — не истории. */
typedef struct
{
    indication_task_t task;
    sul_result_t result;
} render_msg_t;

static QueueHandle_t g_s_render_queue;

/* ── Диагностика трейлера слота (read-only, безопасно звать многократно) ── */

static void log_slot_status(const char *p_when)
{
    const struct flash_area *p_fap;
    const int RC_OPEN = flash_area_open((uint8_t) APP_OWN_SLOT_ID, &p_fap);
    if (RC_OPEN != 0)
    {
        LOG_E(LOG_TAG, "%s: flash_area_open(slot%d) rc=%d", p_when, APP_OWN_SLOT_ID, RC_OPEN);
        return;
    }

    struct boot_swap_state st = { 0 };
    const int RC_RD           = boot_read_swap_state(p_fap, &st);
    LOG_I(LOG_TAG, "%s: slot%d magic=%d copy_done=%d image_ok=%d (rd=%d)", p_when, APP_OWN_SLOT_ID,
          st.magic, st.copy_done, st.image_ok, RC_RD);

    flash_area_close(p_fap);
}

/**
 * @brief Подтвердить СОБСТВЕННЫЙ слот (APP_OWN_SLOT_ID).
 *
 * boot_set_next(fap, active=true, confirm=true), НЕ boot_set_confirmed(): та
 * жёстко пишет в FLASH_AREA_IMAGE_PRIMARY (Slot A) независимо от исполняемого
 * слота — для Direct-XIP с двумя слотами это подтвердило бы не тот при
 * исполнении из Slot Б.
 */
static void confirm_self(void)
{
    log_slot_status("before-confirm"); /* ожидаем magic=1(GOOD) image_ok=3(UNSET) */

    const struct flash_area *p_fap;
    const int RC_OPEN = flash_area_open((uint8_t) APP_OWN_SLOT_ID, &p_fap);
    if (RC_OPEN != 0)
    {
        LOG_E(LOG_TAG, "confirm: flash_area_open(slot%d) rc=%d", APP_OWN_SLOT_ID, RC_OPEN);
        return;
    }

    const int RC_SET = boot_set_next(p_fap, true, true);
    LOG_I(LOG_TAG, "confirm: boot_set_next rc=%d", RC_SET);

    flash_area_close(p_fap);

    log_slot_status("after-confirm"); /* ожидаем image_ok=1(SET) */
}

/**
 * @brief SDRAM (SEMC, без DCD) + framebuffer + CAN — до создания render_task.
 *
 * bsp_sdram_configure()+init() — та же документированная в bsp/sdram.h связка
 * для «прошивок без DCD», которую использует и bootloader-smoke-test.
 */
static bool bring_up_display_and_can(void)
{
    if (bsp_sdram_configure() != BSP_OK)
    {
        LOG_E(LOG_TAG, "bsp_sdram_configure FAILED");
        return false;
    }
    if (bsp_sdram_init() != BSP_OK)
    {
        LOG_E(LOG_TAG, "bsp_sdram_init FAILED");
        return false;
    }
    if (gfx_init(PANEL_TYPE) != BSP_OK)
    {
        LOG_E(LOG_TAG, "gfx_init FAILED");
        return false;
    }
    if (sul_transport_can_init() != BSP_OK)
    {
        LOG_E(LOG_TAG, "sul_transport_can_init FAILED");
        return false;
    }

    LOG_I(LOG_TAG, "display+CAN bring-up OK");
    return true;
}

static void render_task(void *p_arg)
{
    (void) p_arg;

    /* Безусловная первая отрисовка — не ждём первого diff (см. ui/fallback.h). */
    const sul_result_t INITIAL = sul_default_state();
    ui_fallback_render_initial(&INITIAL);

    render_msg_t msg;
    for (;;)
    {
        if (xQueueReceive(g_s_render_queue, &msg, portMAX_DELAY) == pdTRUE)
        {
            ui_fallback_render(&msg.task, &msg.result);
        }
    }
}

/* TEMP (Фаза 3.1): проверка save/персист через power cycle. Первый запуск —
 * пишет сентинел и просит перезагрузку; после перезагрузки load() читает его с
 * флеша → «PERSIST OK». Сентинел остаётся (инертен), затирается первым
 * сохранением из меню (3.2) или factory-reset. УБРАТЬ вместе с #define в 3.2. */
static void settings_selftest(void)
{
    settings_t *p_s = settings_store_get_mutable();
    if (p_s->user.max_load_kg == SETTINGS_SELFTEST_SENTINEL)
    {
        LOG_I(LOG_TAG, "settings self-test: PERSIST OK (сентинел пережил перезагрузку)");
        return;
    }

    p_s->user.max_load_kg = SETTINGS_SELFTEST_SENTINEL;
    const bsp_status_t RC = settings_store_save();
    LOG_I(LOG_TAG,
          "settings self-test: записан сентинел rc=%d — СДЕЛАЙТЕ POWER-CYCLE для проверки персиста",
          RC);
}

static void sul_rx_task(void *p_arg)
{
    (void) p_arg;

    /* LPUART1/MCU-Link VCOM — доступен сразу, без enumeration/wait (в отличие
     * от target-side USB CDC). log_mutex не нужен — единственный писатель. */
    (void) bsp_uart_host_init(115200U);
    log_uart_init();

    /* flash_map_backend требует bsp_qspi_init() ДО любой flash_area_*. */
    const bool QSPI_OK = (bsp_qspi_init() == BSP_OK);
    LOG_I(LOG_TAG, "tft_app phase1 boot: qspi=%s", QSPI_OK ? "OK" : "FAIL");

    /* Настройки ядра (§8, §10): с флеша если QSPI поднялся, иначе дефолты. */
    if (QSPI_OK)
    {
        const bsp_status_t S_RC = settings_store_load();
        LOG_I(LOG_TAG, "settings: load rc=%d proto_addr=%u", S_RC,
              settings_store_get()->user.proto_slice[0]);
        settings_selftest(); /* TEMP (3.1): HW-проверка save/персист, убрать в 3.2 */
    }
    else
    {
        settings_store_init_defaults();
    }

    /* «Дошёл до устойчивого состояния» — сбрасывает счётчик попыток загрузки
     * (recovery загрузчика). SRC GPR, без flash. Безусловно, до потенциально
     * рискованного bring-up дисплея/CAN ниже. */
    bsp_boot_health_mark();

    if (QSPI_OK)
    {
        confirm_self();
    }
    else
    {
        LOG_E(LOG_TAG, "qspi_init FAILED — self-confirm skipped, slot will revert");
    }

    const bool DISPLAY_CAN_OK = bring_up_display_and_can();

    nku_can_ctx_t nku_ctx;
    nku_can_init(&nku_ctx);
    /* Адрес станции из настроек (§8, proto_slice[0]) → сдвиг ID пакетов. */
    nku_can_set_address(&nku_ctx, settings_store_get()->user.proto_slice[0]);

    controller_ctx_t ctrl_ctx;
    controller_init(&ctrl_ctx);

    const TickType_t FEED_PERIOD = pdMS_TO_TICKS(WDOG_FEED_PERIOD_MS);
    uint32_t elapsed_ms          = 0U;
    uint32_t since_status_ms     = 0U;
    TickType_t last_wake         = xTaskGetTickCount();
    TickType_t last_frame_tick   = xTaskGetTickCount();

    for (;;)
    {
        bsp_wdog_refresh();

        elapsed_ms += WDOG_FEED_PERIOD_MS;
        if (elapsed_ms >= HEARTBEAT_PERIOD_MS)
        {
            elapsed_ms = 0U;
            bsp_led_toggle(LED_APP);
        }

        since_status_ms += WDOG_FEED_PERIOD_MS;
        if (since_status_ms >= STATUS_LOG_PERIOD_MS)
        {
            since_status_ms = 0U;
            log_slot_status("periodic");
        }

        if (DISPLAY_CAN_OK)
        {
            sul_result_t decoded;
            bool have_update = false;

            sul_frame_t frame;
            if (sul_transport_can_receive(CAN_RX_TIMEOUT_MS, &frame) == BSP_OK)
            {
                const sul_driver_t *p_driver = sul_registry_active();
                if (p_driver->decode(&nku_ctx, &frame, &decoded) == SUL_STATUS_OK)
                {
                    last_frame_tick = xTaskGetTickCount();
                    have_update     = true;
                }
                /* IGNORED/ERR — Фаза 1 их отдельно не обрабатывает, следующая итерация. */
            }

            const uint32_t SINCE_FRAME_MS =
                (uint32_t) (xTaskGetTickCount() - last_frame_tick) * portTICK_PERIOD_MS;
            if (SINCE_FRAME_MS >= CONNECTION_TIMEOUT_MS)
            {
                /* poll + timeout→default (ARCH, поток данных) — controller сам
                 * определит, реальное ли это изменение (не сработает повторно
                 * на каждой итерации после первого перехода в default). */
                decoded     = sul_default_state();
                have_update = true;
            }

            if (have_update)
            {
                const indication_task_t DIFF = controller_process(&ctrl_ctx, &decoded);
                if (DIFF.pos_pending || DIFF.direction_pending)
                {
                    const render_msg_t MSG = { .task = DIFF, .result = decoded };
                    (void) xQueueOverwrite(g_s_render_queue, &MSG);
                }
            }
        }

        vTaskDelayUntil(&last_wake, FEED_PERIOD);
    }
}

int main(void)
{
    board_hw_init(); /* BOARD_ConfigMPU + BOARD_InitPins + BOARD_BootClockRUN */
    bsp_led_init();

    g_s_render_queue = xQueueCreate(1, sizeof(render_msg_t));
    configASSERT(g_s_render_queue != NULL);

    /* sul_rx выше приоритетом render — приём CAN/WDOG важнее своевременности
     * перерисовки. Стеки x6 (3 КБ) с запасом (см. PLAN.md, Фаза 0 — тонкий
     * стек уже маскировался под похожий на зависание симптом). */
    (void) xTaskCreate(sul_rx_task, "sul_rx", configMINIMAL_STACK_SIZE * 6U, NULL,
                       tskIDLE_PRIORITY + 2U, NULL);
    (void) xTaskCreate(render_task, "render", configMINIMAL_STACK_SIZE * 6U, NULL,
                       tskIDLE_PRIORITY + 1U, NULL);

    vTaskStartScheduler();

    /* Сюда планировщик не возвращается. Если вернулся — не хватило кучи под
     * idle/timer задачу. WDOG сбросит плату. */
    for (;;)
    {
    }
}

/* ── FreeRTOS hooks (строгая диагностика) ────────────────────────────────── */

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void) task;
    (void) name;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
        /* WDOG сбросит плату — детерминированный отказ вместо тихой порчи. */
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
