/**
 * @file  app_tasks.h
 * @brief Контракт между задачами app-слоя (wiring, ARCH §4).
 *
 * Четыре задачи (найдено на HW-верификации Фазы 3.2.4 — меню и render в одной
 * задаче оказались взаимно неотзывчивы, см. PLAN.md):
 *   - bringup_task — одноразовая инициализация (UART/QSPI/settings/self-confirm/
 *     SDRAM+gfx+CAN), затем создаёт три нижеследующие задачи и удаляет себя.
 *   - sul_rx_task  — приём CAN → decode → controller. Heartbeat (§3.7) и
 *     LED-heartbeat отмечаются БЕЗУСЛОВНО; сама CAN-работа — под
 *     !g_menu_active (мягкая пауза на время меню — живость задачи от паузы
 *     не зависит по конструкции).
 *
 * WATCHDOG кормит НЕ задача, а демон программных таймеров (input_poll_cb), и
 * только когда свежи heartbeat'ы ВСЕХ трёх наблюдаемых задач — см.
 * app/heartbeat.h (§3.7). Раньше кормление стояло в sul_rx_task, задаче с
 * самым НИЗКИМ приоритетом: гарантия сводилась к «жив кормилец» и ничего не
 * говорила о живости остальных.
 *   - menu_task    — модель меню: потребление кнопок, hold-to-enter,
 *     навигация/edit/exit+save. НЕ рисует.
 *   - render_task  — единственный владелец дисплея/вызывающий gfx_present().
 *     Event-driven (будится xTaskNotifyGive от sul_rx_task, menu_task И
 *     dispatcher-колбэка opto, §3.4 — MPSC, три продюсера), не поллит, не
 *     содержит кнопочной логики.
 *
 * dispatcher (диспетчерские опто-входы, app/dispatcher.c) — НЕ задача (нет
 * своего while(true)): dispatcher_poll() зовётся из input_poll_cb (тот же
 * софт-таймер, что button, см. task_menu.c) на КАЖДОМ тике, безусловно —
 * непрерывный опрос bsp_opto_read(), не реакция на колбэк (см. докстрок
 * dispatcher.c про баг реактивной версии на стенде). Пишет
 * g_dispatcher_indication и будит render_task напрямую из контекста демона
 * программных таймеров, если итог изменился.
 *
 * main.c создаёt только очередь/софт-таймер ввода/bringup_task — сами задачи
 * друг друга создают/не создают по схеме выше, main.c про это не знает.
 */

#ifndef APP_TASKS_H_
#define APP_TASKS_H_

#include "FreeRTOS.h"
#include "domain/controller.h"     /* indication_task_t */
#include "domain/elevator_model.h" /* sul_result_t      */
#include "menu/menu.h"             /* menu_ctx_t         */
#include "queue.h"
#include "task.h"
#include "timers.h"
#include "ui/boot_screen.h" /* boot_screen_info_t */
#include "ui/fallback.h"    /* dispatcher_indication_t */

#include <stdbool.h>

/* ── Приоритеты (единая точка правды, tskIDLE_PRIORITY-относительно) ────────
 *
 * menu_task > render_task > sul_rx_task. Найдено на HW-верификации (см.
 * PLAN.md): `bsp_can_receive()` — busy-spin БЕЗ yield (bsp/can/src/can.c,
 * никаких блокирующих FreeRTOS-вызовов внутри `for(;;){poll;...}`); пока нет
 * CAN-трафика, sul_rx_task занимает весь CAN_RX_TIMEOUT_MS каждую итерацию, и
 * xTaskDelayUntil() в этом случае НЕ блокирует вовсе (дедлайн уже в прошлом —
 * см. tasks.c, xShouldDelay остаётся false). Более низкоприоритетная задача
 * никогда не получает CPU, пока такая задача продолжает быть READY —
 * render_task, будучи ниже sul_rx_task, банально не мог выполниться (ни
 * первый рендер при старте без связи, ни обработка «--» при обрыве связи).
 * При живом трафике это было невидимо (receive почти всегда быстрый,
 * sul_rx_task реально блокируется), поэтому не всплывало раньше.
 *
 * menu_task остаётся ВЫШЕ render_task: её собственный busy-wait внутри PXP
 * (gfx_pxp_run, ~60-100 мс по замерам) не должен придерживать ввод — то же
 * свойство, ради которого меню и рендер разведены по разным задачам.
 * bringup_task — выше всех троих (монополизирует CPU на время одноразовой
 * инициализации, пока остальные задачи ещё не созданы). Демон программных
 * таймеров (bsp_button debounce, input_poll_cb) — отдельно, на
 * configTIMER_TASK_PRIORITY (см. FreeRTOSConfig.h — наивысший в системе), не
 * отсюда.
 *
 * Корень (busy-spin в bsp_can_receive без yield) НЕ тронут — это отдельный,
 * более рискованный шаг (bsp_can — общий модуль, используется и bare-metal
 * таргетами; добавить FreeRTOS-yield внутрь потребует условной компиляции,
 * как bsp_tick/log_mutex). Обсудить отдельно, если приоритетов недостаточно. */
#define APP_PRIORITY_BRINGUP (tskIDLE_PRIORITY + 4U)
#define APP_PRIORITY_MENU    (tskIDLE_PRIORITY + 3U)
#define APP_PRIORITY_RENDER  (tskIDLE_PRIORITY + 2U)
#define APP_PRIORITY_SUL_RX  (tskIDLE_PRIORITY + 1U)

/** Общий стек-бюджет app-тасков (см. PLAN.md, Фаза 0 — тонкий стек уже
 *  маскировался под похожий на зависание симптом; x8 — с запасом,
 *  проверено на всех четырёх ролях). */
#define APP_TASK_STACK_WORDS (configMINIMAL_STACK_SIZE * 8U)

/**
 * @brief Текущее время в мс — для heartbeat-супервизора (§3.7, app/heartbeat.h).
 *
 * Единая точка перевода тиков FreeRTOS в миллисекунды: супервизор — чистый C
 * (host-тестируется) и время получает ПАРАМЕТРОМ, поэтому переводить обязан
 * вызывающий — и делать это одинаково во всех местах, а не по-своему в каждом.
 */
static inline uint32_t app_now_ms(void)
{
    return (uint32_t) xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/**
 * @brief Самое свежее состояние индикации (не история): sul_rx_task → render_task.
 *
 * Очередь глубины 1 с xQueueOverwrite() — важно только ПОСЛЕДНЕЕ состояние,
 * не промежуточные кадры (рендер не обязан успевать за каждым). Пробуждение
 * render_task — отдельно, через xTaskNotifyGive (см. g_render_task_handle).
 */
typedef struct
{
    indication_task_t task;
    sul_result_t result;
} render_msg_t;

/** Очередь глубины 1 (создаётся в main). Продюсер — sul_rx_task, консюмер — render_task. */
extern QueueHandle_t g_render_queue;

/** Дисплей+CAN подняты (bring-up в bringup_task) — sul_rx_task/render_task ждут этого. */
extern volatile bool g_display_ready;

/** true, пока меню открыто. Мягкая пауза sul_rx_task (CAN-decode/controller/
 *  очередь пропускаются, WDOG/heartbeat — нет, см. task_sul_rx.c). Единственный
 *  писатель — menu_task, единственный читатель — sul_rx_task. */
extern volatile bool g_menu_active;

/** Модель меню. Владеет и мутирует ТОЛЬКО menu_task; render_task только читает
 *  для отрисовки (после xTaskNotifyGive — happens-before через нотификацию,
 *  как у g_render_queue). */
extern menu_ctx_t g_menu;

/** Хэндл render_task — sul_rx_task, menu_task и dispatcher-колбэк opto
 *  (MPSC-продюсеры, §3.4) будят его xTaskNotifyGive() на любое изменение
 *  состояния. Устанавливается bringup_task ДО создания sul_rx_task/menu_task. */
extern TaskHandle_t g_render_task_handle;

/**
 * @brief Диспетчерский вход (opto IN1/IN2, §3.4) — «локальный вход» (ARCH §8
 *        п.3, не данные СУЛ), высший приоритет из всех режимов индикации.
 *
 * Единственный писатель — dispatcher_poll() (app/dispatcher.c), вызывается
 * БЕЗУСЛОВНО на каждом тике input_poll_cb — контекст демона программных
 * таймеров, наивысший приоритет в системе; единственный читатель —
 * render_task (см. task_render.c — снимок в локальную переменную один раз за
 * итерацию, тот же приём, что чинили для гонки курсора меню, см. PLAN.md).
 */
extern volatile dispatcher_indication_t g_dispatcher_indication;

/** Одноразовая инициализация (UART/QSPI/settings/confirm_self/SDRAM+gfx+CAN),
 *  затем создаёт sul_rx_task/menu_task/render_task и удаляет себя. */
void bringup_task(void *p_arg);

/** Приём CAN + WDOG/heartbeat (безусловно) + decode/controller (под !g_menu_active). */
void sul_rx_task(void *p_arg);

/** Модель меню: потребление кнопок, hold-to-enter, навигация/edit/exit+save. */
void menu_task(void *p_arg);

/** Презентация: владелец дисплея, единственный вызывающий gfx_present(). */
void render_task(void *p_arg);

/** Колбэк софт-таймера debounce (bsp_button_poll + bsp_opto_process, §3.4) —
 *  main создаёт таймер, каденция в main. */
void input_poll_cb(TimerHandle_t x_timer);

/**
 * @brief Инициализировать диспетчерский вход (opto IN1/IN2, §3.4).
 *
 * bsp_opto_init() (без колбэков — см. dispatcher.c) + захват начального
 * состояния пинов в g_dispatcher_indication (иначе если вызов уже активен на
 * момент старта устройства — первый dispatcher_poll() ещё не случился, а до
 * него экран не должен показывать NONE поверх уже висящего сигнала).
 * Вызывать из main(), сразу после bsp_button_init() (пины уже настроены в
 * BOARD_InitPins, как и у button) — до старта планировщика.
 */
void dispatcher_init(void);

/**
 * @brief Опросить диспетчерский вход (opto IN1/IN2, §3.4) и обновить
 *        g_dispatcher_indication.
 *
 * Вызывать БЕЗУСЛОВНО на каждом тике input_poll_cb, после bsp_opto_process()
 * — непрерывный опрос bsp_opto_read(), не реакция на колбэк bsp_opto (см.
 * докстрок dispatcher.c: реактивная версия залипала на стенде). Будит
 * render_task, только если итоговая индикация реально изменилась.
 */
void dispatcher_poll(void);

/** Имя состояния диспетчерского входа для логов ("CALL"/"ANSWER"/"NONE").
 *  Логирует render_task, а НЕ dispatcher_poll(): у демона таймеров стек 1 КБ
 *  против 4 КБ у задач, vsnprintf там опасен (см. dispatcher.c). */
const char *dispatcher_indication_name(dispatcher_indication_t v);

/**
 * @brief Что показать на загрузочном экране (§3.10).
 *
 * Заполняет ОДИН раз bringup_task (данные тянутся из четырёх мест — заголовок
 * образа, настройки, реестр протоколов, крэш-запись, т.е. это композиция, а не
 * презентация — ARCH §4); читает render_task. Заполняется ДО создания
 * render_task, дальше не меняется — синхронизация не нужна.
 *
 * Строковые поля указывают на статику (литералы, имена из реестра, поля
 * снятой crash_info_t) — время жизни бесконечное.
 */
extern boot_screen_info_t g_boot_screen_info;

/**
 * @brief Версия ЭТОГО образа из заголовка MCUboot своего слота (§3.10).
 *
 * Единственный источник правды: то, чем подписан образ, и то, по чему
 * загрузчик выбирает слот (docs/RELEASE_PROCESS.md §1.1). Отдельного
 * `version.h` нет намеренно — два места разъезжаются, заголовок образа
 * разъехаться не может.
 *
 * @return false — заголовок не прочитался или magic не совпал; выходные
 *         значения не тронуты, экран покажет «н/д» вместо выдуманного.
 */
bool app_image_version(uint8_t *p_major, uint8_t *p_minor, uint16_t *p_revision);

/**
 * @brief Применить уровень логов из настроек (§3.9).
 *
 * Переводит ИНДЕКС пункта меню (`settings_device_t.log_level`: 0 Выкл /
 * 1 Инфо / 2 Отладка) в `LOG_LEVEL_*` и зовёт `log_set_level()`. Перевод живёт
 * в app-слое (wiring, ARCH §4), а не в `settings_store` и не в `utils/log`:
 * первый не должен знать про логгер, второй — про меню. Единственная точка,
 * чтобы bringup и меню не разъехались в трактовке.
 *
 * Определена в task_bringup.c (как log_slot_status()).
 */
void app_log_level_apply(uint8_t setting_index);

/** Диагностика трейлера слота (read-only, безопасно звать многократно) —
 *  общая для bringup_task (before/after-confirm) и sul_rx_task (периодический
 *  re-log). Определена в task_bringup.c. */
void log_slot_status(const char *p_when);

#endif /* APP_TASKS_H_ */
