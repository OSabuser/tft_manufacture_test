/**
 * @file  main.c
 * @brief tft_app — точка входа. Фаза 0 (каркас): FreeRTOS + heartbeat + WDOG +
 *        рантайм self-confirm слота MCUboot + UART диагностика.
 *
 * Каркас walking-skeleton: образ линкуется как валидный MCUboot-слот
 * (Direct-XIP), bootloader в него прыгает, FreeRTOS стартует, LED мигает,
 * WDOG кормится, образ ПОДТВЕРЖДАЕТ СЕБЯ в рантайме (нет revert на повторной
 * загрузке — anti-brick сохранён). Бизнес-логика — со следующих фаз (PLAN.md).
 *
 * Контракт со стороны bootloader (см. firmware/bootloader/test_stub/main.c):
 *   - WDOG взведён загрузчиком, WDE write-once — образ ОБЯЗАН кормить его.
 *   - Образ подписан imgtool БЕЗ --confirm → обязан подтвердить себя рантаймом
 *     (boot_set_next на своём слоте), иначе MCUboot откатит его.
 *
 * Записью QSPI из app под Direct-XIP можно пользоваться только потому, что
 * ramfunc-код bsp_qspi реально копируется в ITCM — для этого потребитель
 * ОБЯЗАН задать __STARTUP_INITIALIZE_RAMFUNCTION (см. CMakeLists и
 * bsp/qspi_flash/README.md). Без него запись виснет в qspi_write_fifo.
 *
 * ── Диагностика: UART (LPUART1 / MCU-Link VCOM), НЕ USB CDC ──────────────
 * USB CDC (target-side EHCI/PHY) под FreeRTOS не заработал стабильно —
 * несколько заходов, последний раз плата падала в recovery загрузчика сразу
 * после добавления wait-for-host цикла (вероятная причина — не сама USB, а
 * инкрементально узкий стек boot_task, но целый класс неопределённости
 * "первый в репозитории FreeRTOS-потребитель USB device stack" остаётся).
 * LPUART1 — самый проверенный канал в репозитории (используется во ВСЕХ
 * tests/target/* HIL-образах), идёт через уже подключённый для SWD кабель
 * MCU-Link, не требует enumeration/wait, пины уже смуксены в BOARD_InitPins().
 * Если понадобится USB CDC под FreeRTOS (например, Фаза 7 — provisioning) —
 * отдельная сфокусированная задача, не диагностика Фазы 0.
 *
 * main() — минимальный bare-metal (board bring-up + запуск планировщика). Всё
 * остальное — в boot_task, после старта планировщика (унификация с QSPI/
 * confirm-логикой; UART сам по себе не требует тика для init/write).
 */

#include "FreeRTOS.h"
#include "board.h"
#include "bootutil/bootutil_public.h"
#include "bsp/boot_state.h"
#include "bsp/led.h"
#include "bsp/qspi_flash.h"
#include "bsp/uart_host.h"
#include "bsp/wdog.h"
#include "flash_map.h"
#include "log/log.h"
#include "port/log_uart.h"
#include "task.h"

#include <stdbool.h>

/* Фаза 0: диагностика self-confirm по UART (LPUART1 / MCU-Link VCOM,
 * 115200). Сократить/убрать, когда подтверждено на железе многократно. */
#define LOG_TAG "app"

/* Собственный слот образа. Slot A = 0 (primary), Slot Б = 1 (secondary). */
#ifndef APP_OWN_SLOT_ID
#define APP_OWN_SLOT_ID 0
#endif

#define HEARTBEAT_PERIOD_MS  500U
#define WDOG_FEED_PERIOD_MS  100U  /* кормим чаще периода мигания — таймаут WDOG >= 1 c */
#define STATUS_LOG_PERIOD_MS 2000U /* периодический re-log трейлера — виден независимо
                                    * от момента подключения терминала (UART без
                                    * handshake — писать можно сразу, слушают или нет) */

/* ── Диагностика трейлера слота (read-only, безопасно звать многократно) ── */

static void log_slot_status(const char *p_when)
{
    const struct flash_area *p_fap;
    const int                RC_OPEN = flash_area_open((uint8_t) APP_OWN_SLOT_ID, &p_fap);
    if (RC_OPEN != 0)
    {
        LOG_E(LOG_TAG, "%s: flash_area_open(slot%d) rc=%d", p_when, APP_OWN_SLOT_ID, RC_OPEN);
        return;
    }

    struct boot_swap_state st    = {0};
    const int              RC_RD = boot_read_swap_state(p_fap, &st);
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
    const int                RC_OPEN = flash_area_open((uint8_t) APP_OWN_SLOT_ID, &p_fap);
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

static void boot_task(void *p_arg)
{
    (void) p_arg;

    /* LPUART1/MCU-Link VCOM — доступен сразу, без enumeration/wait (в отличие
     * от target-side USB CDC). log_mutex не нужен — единственная задача. */
    (void) bsp_uart_host_init(115200U);
    log_uart_init();

    /* flash_map_backend требует bsp_qspi_init() ДО любой flash_area_*. */
    const bool QSPI_OK = (bsp_qspi_init() == BSP_OK);
    LOG_I(LOG_TAG, "tft_app phase0 boot: qspi=%s", QSPI_OK ? "OK" : "FAIL");

    /* «Дошёл до устойчивого состояния» — сбрасывает счётчик попыток загрузки
     * (recovery загрузчика). SRC GPR, без flash. */
    bsp_boot_health_mark();

    if (QSPI_OK)
    {
        confirm_self();
    }
    else
    {
        LOG_E(LOG_TAG, "qspi_init FAILED — self-confirm skipped, slot will revert");
    }

    const TickType_t FEED_PERIOD     = pdMS_TO_TICKS(WDOG_FEED_PERIOD_MS);
    uint32_t         elapsed_ms      = 0U;
    uint32_t         since_status_ms = 0U;
    TickType_t       last_wake       = xTaskGetTickCount();

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

        vTaskDelayUntil(&last_wake, FEED_PERIOD);
    }
}

int main(void)
{
    board_hw_init(); /* BOARD_ConfigMPU + BOARD_InitPins + BOARD_BootClockRUN */
    bsp_led_init();

    /* x4 (2 КБ) с запасом: логирование (vsnprintf) + чтение трейлера. */
    (void) xTaskCreate(boot_task, "boot", configMINIMAL_STACK_SIZE * 4U, NULL,
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
