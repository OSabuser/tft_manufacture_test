/**
 * @file  sul.h
 * @brief Реестр драйверов СУЛ (ARCH.md §6) — «одна прошивка — много протоколов».
 *
 * Драйвер = чистый декодер (host-тестируемый, без HAL) + отдельный тонкий
 * транспорт-адаптер (HW, живёт в sul/transport/<bus>). Декодер НЕ владеет
 * состоянием сам — состояние (например, накопленная позиция между PACKET1 и
 * PACKET3 у НКУ-CAN) держит caller в ctx и передаёт указатель на каждый вызов
 * decode(). Это позволяет декодеру оставаться описанным одной чистой функцией
 * и не тянуть за собой выделение памяти/жизненный цикл.
 *
 * Активный протокол выбирается из настроек (`settings_device_t.protocol_id`,
 * ARCH §8) через `sul_registry_set_active()` — вызывающий (app-слой) читает
 * настройки и толкает id сюда; сам registry настройки не читает (домен не
 * знает про settings_store, см. паттерн `nku_can_set_address()`).
 */

#ifndef DOMAIN_SUL_H_
#define DOMAIN_SUL_H_

#include "domain/elevator_model.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** Результат decode() одного кадра/пакета. */
    typedef enum
    {
        SUL_STATUS_OK = 0, /**< кадр распознан, ctx и *p_out обновлены          */
        SUL_STATUS_IGNORED, /**< кадр не для этого драйвера — *p_out не тронут   */
        SUL_STATUS_ERR, /**< кадр совпал по ID, но малформирован (DLC и т.п.)  */
    } sul_status_t;

    /**
 * @brief Кадр транспортного уровня, нейтральный к шине (CAN/UART/...).
 *
 * Транспорт-адаптер (HW) заполняет её из своего протокола (для CAN — id и
 * data/len из bsp_can_frame_t); декодер (чистый C) её только читает.
 */
    typedef struct
    {
        uint32_t id; /**< CAN ID либо адрес/маркер кадра другого транспорта */
        uint8_t bus; /**< на будущее — несколько шин одного типа (0 = единственная) */
        const uint8_t *p_data;
        uint16_t len;
    } sul_frame_t;

    /**
 * @brief Чистая функция декодирования — БЕЗ единого HAL-вызова.
 *
 * @param p_ctx    состояние драйвера (владеет caller, см. докстрок файла)
 * @param p_frame  один кадр транспортного уровня
 * @param p_out    заполняется только при SUL_STATUS_OK
 */
    typedef sul_status_t (*sul_decode_fn_t)(void *p_ctx, const sul_frame_t *p_frame,
                                            sul_result_t *p_out);

    /**
 * @brief `sul_driver_t.connection_timeout_ms` — протокол не поддерживает
 *        детекцию обрыва связи по тишине (шлёт кадры ТОЛЬКО по изменению
 *        состояния на станции, не периодически) — таймаут-логика в
 *        task_sul_rx.c для него отключена целиком, «--» по тишине не
 *        покажется никогда для этого протокола.
 */
#define SUL_CONNECTION_TIMEOUT_DISABLED 0U

    /**
 * @brief Запрос протокола на запись байта в СВОЙ proto_slice (ARCH §8) —
 *        напр. удалённая установка адреса у НКУ-CAN.
 *
 * НЕ путать с decode()/sul_result_t — это отдельный канал специально под
 * settings, не под индикацию.
 */
    typedef struct
    {
        uint8_t slice_offset; /**< куда в settings_t.user.proto_slice[] (§8) */
        uint8_t value;
    } sul_slice_write_t;

    /**
 * @brief Забрать запрос протокола на самостоятельную запись в proto_slice.
 *
 * Чистая функция — читает НАКОПЛЕННОЕ состояние ctx после последнего
 * decode(), settings_store не трогает вообще: домен не пишет настройки сам
 * (см. докстрок файла), только сигнализирует через это; саму запись +
 * идемпотентность (сравнение с текущим сохранённым значением — ОБЩИЙ гейт,
 * одинаковый для любого протокола) делает app-слой (task_sul_rx.c).
 *
 * @param p_ctx  ctx драйвера (см. sul_driver_t.p_ctx)
 * @param p_out  заполняется только при возврате true
 * @return true — есть запрос, *p_out валиден; false — писать нечего.
 */
    typedef bool (*sul_take_pending_write_fn_t)(void *p_ctx, sul_slice_write_t *p_out);

/** Максимум байт полезной нагрузки исходящего кадра (CAN-кадр = 8). */
#define SUL_TX_DATA_MAX 8U

    /**
 * @brief Исходящий кадр, который протокол просит отправить в шину — напр.
 *        обязательный отклик станции у УИМ-6100.
 *
 * Нейтрален к шине, как и `sul_frame_t` на приёме: домен формирует
 * ЧТО отправить, транспорт знает КУДА и КАК (ARCH §4 — домену шина
 * недоступна).
 */
    typedef struct
    {
        uint32_t id; /**< CAN ID либо адрес/маркер кадра другого транспорта */
        uint8_t data[SUL_TX_DATA_MAX];
        uint8_t len; /**< значимых байт в data[], 0..SUL_TX_DATA_MAX        */
    } sul_tx_frame_t;

    /**
 * @brief Забрать запрос протокола на отправку кадра в шину.
 *
 * Симметрична `sul_take_pending_write_fn_t`, но для исходящего трафика:
 * чистая функция, читает НАКОПЛЕННОЕ состояние ctx после последнего decode()
 * и не трогает ни шину, ни HAL. Саму отправку делает app-слой
 * (task_sul_rx.c) через транспорт активного протокола.
 *
 * Запрос ТРАНЗИТЕН: валиден только сразу после decode(), в котором возник.
 * Реализация обязана сбрасывать его в начале каждого decode() — иначе
 * устаревший отклик уедет в шину на следующем, не относящемся к делу кадре.
 *
 * @param p_ctx  ctx драйвера (см. sul_driver_t.p_ctx)
 * @param p_out  заполняется только при возврате true
 * @return true — есть кадр к отправке, *p_out валиден; false — отправлять нечего.
 */
    typedef bool (*sul_take_pending_tx_fn_t)(void *p_ctx, sul_tx_frame_t *p_out);

    /**
 * @brief Тип редактируемого параметра протокола (ARCH §8).
 *
 * Подмножество `menu_item_type_t` — без зависимости domain→menu (§4, домен не
 * знает презентацию); трансляция в `menu_item_type_t` живёт на стороне menu/.
 */
    typedef enum
    {
        SUL_SETTINGS_BYTE = 0U, /**< число min..max                     */
        SUL_SETTINGS_SELECT,    /**< выбор из p_options[0..max]         */
        SUL_SETTINGS_BOOL,      /**< да/нет                             */
    } sul_settings_type_t;

    /**
 * @brief Один параметр протокола — строка данных (ARCH §8), не код.
 *
 * `slice_offset` — смещение ВНУТРИ `settings_t.user.proto_slice[]`
 * (0..SETTINGS_PROTO_SLICE_LEN-1 из settings_store.h), не внутри всего
 * `settings_t` — домен не включает settings_store.h, трансляцию в реальный
 * offsetof() делает menu/ (единственный слой, знающий оба типа).
 */
    typedef struct
    {
        const char *p_label;
        sul_settings_type_t type;
        uint8_t slice_offset;
        uint8_t min; /**< для SELECT/BYTE/BOOL */
        uint8_t max;
        /**
     * @brief Необслуживаемый «разрыв» внутри [min..max] — значения
     *        `gap_from..gap_to` пропускаются при редактировании.
     *
     * Нужен для протоколов, у которых допустимые значения не непрерывны:
     * у УИМ-6100 адрес индикатора — 1..40 (этажный) ∪ 46..50 (роли), а
     * 41..45 зарезервированы. Хранимое значение при этом остаётся НАСТОЯЩИМ
     * значением параметра (напр. адресом) — трансляции индекс↔значение нет
     * нигде, decode()/фильтры дескриптор не читают вообще.
     *
     * `gap_from == 0` — разрыва нет (значение по умолчанию при
     * designated-инициализации, поэтому у большинства протоколов эти поля
     * просто не указываются).
     */
        uint8_t gap_from;
        uint8_t gap_to;
        /** Метки для SELECT/BOOL (p_options[value]); NULL — рендер числом. */
        const char *const *p_options;
    } sul_settings_entry_t;

    /** @brief Набор параметров протокола, регистрируется драйвером (ARCH §8). */
    typedef struct
    {
        const sul_settings_entry_t *p_entries;
        uint8_t count;
    } sul_settings_desc_t;

    typedef struct
    {
        uint8_t id; /**< стабильный идентификатор протокола */
        const char *p_name; /**< для меню (выбор протокола)    */
        sul_decode_fn_t decode;
        /** Параметры протокола для меню (§8); NULL — протокол без настроек. */
        const sul_settings_desc_t *p_settings;
        /**
     * @brief Контекст decode() — статика, владеет драйвер (sul_registry.c),
     *        живёт постоянно (не пересоздаётся при смене активного
     *        протокола). Инициализируется `sul_registry_init()`.
     *
     * Транспорт (какую шину/источник опрашивать) сюда НЕ входит — это
     * HW-специфика, остаётся wiring'ом app-слоя (task_sul_rx.c), чтобы
     * реестр/декодеры оставались host-тестируемыми без bsp.
     */
        void *p_ctx;
        /**
     * @brief Протокол сам инициирует запись в свой proto_slice (§8) — напр.
     *        удалённая адресация. NULL — протокол никогда этого не делает
     *        (большинство); app-слой (task_sul_rx.c) проверяет указатель на
     *        NULL перед вызовом, никакой протокол-специфичной ветки там нет.
     */
        sul_take_pending_write_fn_t take_pending_write;
        /**
     * @brief Протокол сам инициирует отправку кадра в шину — напр.
     *        обязательный отклик станции у УИМ-6100. NULL — протокол
     *        никогда не отвечает (НКУ-CAN, демо): app-слой проверяет
     *        указатель на NULL, protocol-specific ветки для этого нет.
     */
        sul_take_pending_tx_fn_t take_pending_tx;
        /**
     * @brief Таймаут «потери связи» (мс) — сколько task_sul_rx.c ждёт без
     *        валидного кадра ЭТОГО протокола, прежде чем считать связь
     *        потерянной (→ sul_default_state(), «--» на экране).
     *
     * ОБЯЗАТЕЛЬНОЕ поле, не пользовательская настройка: реальный период
     * отправки у станции — знание протокола (разные семейства/станции шлют
     * раз в ~200 мс или раз в ~1 с — оператор этого не знает и не должен
     * настраивать), жёстко задаётся здесь при регистрации в sul_registry.c.
     * `SUL_CONNECTION_TIMEOUT_DISABLED` (0) — протокол event-driven на
     * станции (шлёт только по изменению) — для него понятие «обрыва по
     * тишине» в принципе некорректно, таймаут выключен целиком.
     */
        uint32_t connection_timeout_ms;
    } sul_driver_t;

    /**
 * @brief Идентификаторы протоколов — стабильны, не переиспользовать.
 *
 * Список открыт (ARCH §2.2): УЭЛ, УКЛ, НКУ-SD7, УИМ добавляются в Фазе 8.
 */
    enum
    {
        SUL_PROTOCOL_NKU_CAN = 0U,
        SUL_PROTOCOL_DEMO =
            1U, /**< синтетический источник, ARCH §8 — витрина/тест дескрипторного механизма */
        SUL_PROTOCOL_UIM = 2U,
    };

    /**
 * @brief Инициализировать ctx ВСЕХ зарегистрированных драйверов (вызывать
 *        один раз при bringup, до первого sul_registry_active()->decode()).
 */
    void sul_registry_init(void);

    /**
 * @brief Активный драйвер — см. `sul_registry_set_active()`.
 * @return указатель на статический дескриптор, никогда NULL.
 */
    const sul_driver_t *sul_registry_active(void);

    /**
 * @brief Найти драйвер по id.
 * @return NULL, если протокол не зарегистрирован.
 */
    const sul_driver_t *sul_registry_find(uint8_t id);

    /**
 * @brief Выбрать активный протокол (ARCH §8 — вызывается app-слоем из
 *        `settings_device_t.protocol_id`, дёшево при каждом вызове,
 *        см. паттерн `nku_can_set_address()`/CAN-фильтров).
 *
 * @param id  id протокола. Неизвестный id — игнорируется, активный не меняется
 *            (защита от мусора в настройках; `sul_registry_active()` остаётся
 *            валидным).
 */
    void sul_registry_set_active(uint8_t id);

    /** @brief Число зарегистрированных протоколов (для построения меню). */
    uint8_t sul_registry_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_H_ */
