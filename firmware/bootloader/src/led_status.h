/**
 * @file  led_status.h
 * @brief Индикация bootloader на двух LED — единый словарь паттернов.
 *
 * Полное человекочитаемое описание (для сервисных инженеров) —
 * docs/bootloader/LED_PATTERNS.md. Здесь — программный контракт; периоды
 * в мс заданы в led_status.c и обязаны совпадать с тем документом.
 *
 * Разделение на два вида вызова не случайно:
 *   - Фоновые (устойчивые) состояния рисует главный цикл main.c каждую
 *     итерацию: led_status_draw_background().
 *   - Паттерн «идёт установка» рисуется ИЗНУТРИ блокирующей установки (главный
 *     цикл в это время не исполняется): led_status_tick_install(), обёрнутый в
 *     окно led_status_install_begin()/_end(). Вне окна tick — no-op, поэтому
 *     его безопасно звать из flash_area_erase(), который дёргается и на
 *     revert/recovery-стирании, а не только на установке.
 */

#ifndef LED_STATUS_H_
#define LED_STATUS_H_

/** @brief Фоновое (устойчивое) состояние индикации. */
typedef enum
{
    LED_BG_WAITING,  /**< Норма: HEARTBEAT 50/450, APP выкл — ждём microSD.     */
    LED_BG_HW_FAULT, /**< Неисправность: HEARTBEAT 50/450, APP 100/100.         */
    LED_BG_RECOVERY, /**< Recovery (Фаза 6): оба LED синхронно 100/100.         */
} led_bg_t;

/**
 * @brief Нарисовать фоновый паттерн по текущему тику.
 *
 * Вызывать каждую итерацию главного цикла. Приоритет разрешается на стороне
 * вызывателя (recovery > неисправность > норма) — сюда приходит уже
 * выбранное состояние.
 */
void led_status_draw_background(led_bg_t bg);

/** @brief Открыть окно «идёт установка» — с этого момента tick рисует паттерн. */
void led_status_install_begin(void);

/** @brief Закрыть окно «идёт установка» — tick снова становится no-op. */
void led_status_install_end(void);

/**
 * @brief Обновить APP/HEARTBEAT под паттерн установки (HEARTBEAT 50/450,
 *        APP 250/250). No-op вне окна install.
 *
 * Звать из ВСЕХ блокирующих циклов установки — и поблочного стирания слота
 * (flash_area_erase, ~5 c на 2 МБ), и копирования чанков (sd_update), иначе
 * APP замирал бы на время стирания.
 */
void led_status_tick_install(void);

/**
 * @brief Разовая индикация «образ с SD отклонён»: APP мигает 4×(80/80).
 *
 * Блокирующая (~640 мс), кормит watchdog по ходу. Оставляет APP выключенным —
 * фоновый паттерн восстановит главный цикл на следующей итерации.
 */
void led_status_flash_image_rejected(void);

#endif /* LED_STATUS_H_ */
