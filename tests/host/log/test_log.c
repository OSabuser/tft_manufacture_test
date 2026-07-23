/**
 * @file  test_log.c
 * @brief Unit-тесты для utils/log (Unity + fff).
 *
 * Категория: A с fff-хуками.
 *   log.c не зависит от fsl_*.h, но имеет weak-хуки (mutex, timestamp)
 *   которые мокаются через fff как strong-определения — линкер выбирает
 *   их поверх weak-реализаций из log.c автоматически.
 *
 * Захват вывода:
 *   Вместо реального UART используется статическая функция capture_cb(),
 *   которая сохраняет содержимое и длину каждого вызова в s_capture.
 *   Тесты форматирования проверяют s_capture.buf через strstr().
 *
 * Группы тестов:
 *
 *   1. Init — поведение до и после log_init().
 *      Проверяет что callback не вызывается при NULL-транспорте,
 *      и что повторный log_init() заменяет предыдущий callback.
 *
 *   2. Output — формат строки.
 *      Проверяет наличие символа уровня ([E]/[W]/[I]/[D]/[V]),
 *      тега, текста сообщения с форматированием и \r\n в конце.
 *
 *   3. Timestamp — вызов log_get_timestamp_ms().
 *      Проверяет что хук вызывается ровно один раз на каждый LOG_*
 *      и что возвращённое значение попадает в строку вывода.
 *
 *   4. Overflow — защита от выхода за границу буфера.
 *      Подаёт строку длиннее LOG_BUF_SIZE (256 байт). Проверяет что
 *      callback получил не более 256 байт и строка заканчивается \r\n.
 *      Именно эта группа поймала баг size_t underflow при первом прогоне
 *      через ASan (vsnprintf возвращает желаемую длину, а не фактическую).
 *
 *   5. Mutex — порядок lock → callback → unlock.
 *      custom_fake для lock/unlock записывает монотонный счётчик s_seq,
 *      capture_cb делает то же самое. После вызова LOG_* сравниваем числа:
 *      s_lock_seq < s_cb_seq < s_unlock_seq.
 *
 *   6. Context — передача p_ctx в callback.
 *      Проверяет что указатель контекста из log_init() доходит до
 *      callback без изменений (NULL и ненулевой указатель).
 */

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS;

/* ── fff-фейки для weak-хуков log.c ─────────────────────────────────────
 * Объявляем ДО включения log/log.h, чтобы линкер видел strong-определения
 * раньше weak-реализаций из log.c.                                        */
FAKE_VOID_FUNC(log_mutex_init);
FAKE_VOID_FUNC(log_mutex_lock);
FAKE_VOID_FUNC(log_mutex_unlock);
FAKE_VALUE_FUNC(uint32_t, log_get_timestamp_ms);

#include "log/log.h"

#include <stdint.h>
#include <string.h>

/* ── Захват вывода ───────────────────────────────────────────────────────── */

#define CAPTURE_BUF_SIZE 512U

static struct
{
    char buf[CAPTURE_BUF_SIZE];
    size_t len;
    int call_count;
    void *p_ctx;
} g_s_capture;

static void capture_cb(const char *p_buf, size_t len, void *p_ctx)
{
    g_s_capture.call_count++;
    g_s_capture.p_ctx = p_ctx;

    size_t copy = (len < CAPTURE_BUF_SIZE - 1U) ? len : (CAPTURE_BUF_SIZE - 1U);
    memcpy(g_s_capture.buf, p_buf, copy);
    g_s_capture.buf[copy] = '\0';
    g_s_capture.len       = copy;
}

/* ── setUp / tearDown ────────────────────────────────────────────────────── */

void setUp(void)
{
    memset(&g_s_capture, 0, sizeof(g_s_capture));

    RESET_FAKE(log_mutex_init);
    RESET_FAKE(log_mutex_lock);
    RESET_FAKE(log_mutex_unlock);
    RESET_FAKE(log_get_timestamp_ms);
    FFF_RESET_HISTORY();

    /* Каждый тест начинает с чистого состояния логгера */
    log_init(capture_cb, NULL);
    log_set_enabled(true); /* тумблер §3.6 — не сбрасывается log_init(), сбросить явно */
}

void tearDown(void)
{
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 1. Init — поведение до и после log_init()
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_no_output_before_init(void)
{
    /* Сбросить callback — имитировать состояние до первого log_init() */
    log_init(NULL, NULL);
    memset(&g_s_capture, 0, sizeof(g_s_capture));

    LOG_I("TAG", "message");

    TEST_ASSERT_EQUAL_INT(0, g_s_capture.call_count);
}

void test_no_output_with_null_cb(void)
{
    log_init(NULL, NULL);
    memset(&g_s_capture, 0, sizeof(g_s_capture));

    LOG_W("TAG", "message");

    TEST_ASSERT_EQUAL_INT(0, g_s_capture.call_count);
}

void test_callback_called_once_per_log(void)
{
    LOG_I("TAG", "first");
    TEST_ASSERT_EQUAL_INT(1, g_s_capture.call_count);

    LOG_I("TAG", "second");
    TEST_ASSERT_EQUAL_INT(2, g_s_capture.call_count);
}

void test_reinit_replaces_callback(void)
{
    /* После повторного log_init() с NULL — вывода нет */
    log_init(NULL, NULL);

    int count_before = g_s_capture.call_count;
    LOG_I("TAG", "should not appear");

    TEST_ASSERT_EQUAL_INT(count_before, g_s_capture.call_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 2. Output — формат строки: уровень, тег, перевод строки
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_output_contains_level_error(void)
{
    LOG_E("TAG", "msg");
    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "[E]"));
}

void test_output_contains_level_warn(void)
{
    LOG_W("TAG", "msg");
    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "[W]"));
}

void test_output_contains_level_info(void)
{
    LOG_I("TAG", "msg");
    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "[I]"));
}

void test_output_contains_level_debug(void)
{
    LOG_D("TAG", "msg");
    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "[D]"));
}

void test_output_contains_level_verbose(void)
{
    LOG_V("TAG", "msg");
    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "[V]"));
}

void test_output_contains_tag(void)
{
    LOG_I("BOOT", "msg");
    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "[BOOT]"));
}

void test_output_contains_message(void)
{
    LOG_I("TAG", "hello world");
    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "hello world"));
}

void test_output_ends_with_crlf(void)
{
    LOG_I("TAG", "msg");

    TEST_ASSERT_GREATER_THAN(2U, g_s_capture.len);
    TEST_ASSERT_EQUAL_CHAR('\r', g_s_capture.buf[g_s_capture.len - 2U]);
    TEST_ASSERT_EQUAL_CHAR('\n', g_s_capture.buf[g_s_capture.len - 1U]);
}

void test_output_format_message_with_args(void)
{
    LOG_I("TAG", "val=%d", 42);
    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "val=42"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 3. Timestamp — вызов log_get_timestamp_ms() и значение в выводе
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_timestamp_hook_called(void)
{
    LOG_I("TAG", "msg");
    TEST_ASSERT_EQUAL_INT(1, log_get_timestamp_ms_fake.call_count);
}

void test_timestamp_hook_called_each_log(void)
{
    LOG_I("TAG", "first");
    LOG_I("TAG", "second");
    TEST_ASSERT_EQUAL_INT(2, log_get_timestamp_ms_fake.call_count);
}

void test_timestamp_value_appears_in_output(void)
{
    log_get_timestamp_ms_fake.return_val = 12345U;

    LOG_I("TAG", "msg");

    TEST_ASSERT_NOT_NULL(strstr(g_s_capture.buf, "12345"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 4. Overflow — буфер переполнен, строка обрезана, \r\n сохранён
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_overflow_output_not_exceeds_buf_size(void)
{
    /* Строка длиннее LOG_BUF_SIZE (256) */
    char long_msg[300];
    memset(long_msg, 'X', sizeof(long_msg) - 1U);
    long_msg[sizeof(long_msg) - 1U] = '\0';

    LOG_I("TAG", "%s", long_msg);

    /* callback получил не больше LOG_BUF_SIZE байт */
    TEST_ASSERT_LESS_OR_EQUAL(256U, g_s_capture.len);
}

void test_overflow_output_ends_with_crlf(void)
{
    /* При переполнении \r\n всё равно должен быть в конце */
    char long_msg[300];
    memset(long_msg, 'Y', sizeof(long_msg) - 1U);
    long_msg[sizeof(long_msg) - 1U] = '\0';

    LOG_I("TAG", "%s", long_msg);

    TEST_ASSERT_GREATER_THAN(2U, g_s_capture.len);
    TEST_ASSERT_EQUAL_CHAR('\r', g_s_capture.buf[g_s_capture.len - 2U]);
    TEST_ASSERT_EQUAL_CHAR('\n', g_s_capture.buf[g_s_capture.len - 1U]);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 5. Mutex — порядок lock → callback → unlock
 *
 * Используем глобальную историю вызовов fff (fff.call_history[]).
 * lock должен быть вызван раньше callback (но callback — не fff-фейк,
 * поэтому порядок проверяем через счётчик последовательности.
 * ═══════════════════════════════════════════════════════════════════════════ */

static int s_seq        = 0;
static int s_lock_seq   = 0;
static int s_cb_seq     = 0;
static int s_unlock_seq = 0;

static void seq_lock(void)
{
    s_lock_seq = ++s_seq;
}
static void seq_unlock(void)
{
    s_unlock_seq = ++s_seq;
}

static void seq_cb(const char *p_buf, size_t len, void *p_ctx)
{
    (void) p_buf;
    (void) len;
    (void) p_ctx;
    s_cb_seq = ++s_seq;
}

void test_mutex_lock_before_callback(void)
{
    s_seq = s_lock_seq = s_cb_seq = s_unlock_seq = 0;
    log_mutex_lock_fake.custom_fake              = seq_lock;
    log_mutex_unlock_fake.custom_fake            = seq_unlock;
    log_init(seq_cb, NULL);

    LOG_I("TAG", "msg");

    TEST_ASSERT_GREATER_THAN(0, s_lock_seq);
    TEST_ASSERT_GREATER_THAN(0, s_cb_seq);
    TEST_ASSERT_LESS_THAN(s_cb_seq, s_lock_seq);
}

void test_mutex_unlock_after_callback(void)
{
    s_seq = s_lock_seq = s_cb_seq = s_unlock_seq = 0;
    log_mutex_lock_fake.custom_fake              = seq_lock;
    log_mutex_unlock_fake.custom_fake            = seq_unlock;
    log_init(seq_cb, NULL);

    LOG_I("TAG", "msg");

    TEST_ASSERT_GREATER_THAN(s_cb_seq, s_unlock_seq);
}

void test_mutex_lock_unlock_called_once_per_log(void)
{
    LOG_I("TAG", "msg");

    TEST_ASSERT_EQUAL_INT(1, log_mutex_lock_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, log_mutex_unlock_fake.call_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 6. Context — p_ctx передаётся в callback без изменений
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_ctx_null_passed_to_callback(void)
{
    log_init(capture_cb, NULL);
    LOG_I("TAG", "msg");
    TEST_ASSERT_NULL(g_s_capture.p_ctx);
}

void test_ctx_pointer_passed_to_callback(void)
{
    int dummy = 42;
    log_init(capture_cb, &dummy);

    LOG_I("TAG", "msg");

    TEST_ASSERT_EQUAL_PTR(&dummy, g_s_capture.p_ctx);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 7. Runtime toggle — рантайм-тумблер поверх компайл-тайм LOG_LEVEL (§3.6)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_enabled_by_default(void)
{
    LOG_I("TAG", "message");
    TEST_ASSERT_EQUAL_INT(1, g_s_capture.call_count);
}

void test_is_enabled_reflects_state(void)
{
    TEST_ASSERT_TRUE(log_is_enabled());
    log_set_enabled(false);
    TEST_ASSERT_FALSE(log_is_enabled());
    log_set_enabled(true);
    TEST_ASSERT_TRUE(log_is_enabled());
}

void test_disabled_suppresses_output(void)
{
    log_set_enabled(false);
    LOG_I("TAG", "message");
    TEST_ASSERT_EQUAL_INT(0, g_s_capture.call_count);
}

void test_reenabled_resumes_output(void)
{
    log_set_enabled(false);
    LOG_I("TAG", "swallowed");
    log_set_enabled(true);
    LOG_I("TAG", "visible");

    TEST_ASSERT_EQUAL_INT(1, g_s_capture.call_count);
}

void test_disabled_does_not_take_mutex(void)
{
    log_set_enabled(false);
    LOG_I("TAG", "message");

    /* Гейт — раньше форматирования/мьютекса (дёшево при выключенном тумблере). */
    TEST_ASSERT_EQUAL_INT(0, log_mutex_lock_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, log_mutex_unlock_fake.call_count);
}

/* ── Runner ──────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    /* Init */
    RUN_TEST(test_no_output_before_init);
    RUN_TEST(test_no_output_with_null_cb);
    RUN_TEST(test_callback_called_once_per_log);
    RUN_TEST(test_reinit_replaces_callback);

    /* Output */
    RUN_TEST(test_output_contains_level_error);
    RUN_TEST(test_output_contains_level_warn);
    RUN_TEST(test_output_contains_level_info);
    RUN_TEST(test_output_contains_level_debug);
    RUN_TEST(test_output_contains_level_verbose);
    RUN_TEST(test_output_contains_tag);
    RUN_TEST(test_output_contains_message);
    RUN_TEST(test_output_ends_with_crlf);
    RUN_TEST(test_output_format_message_with_args);

    /* Timestamp */
    RUN_TEST(test_timestamp_hook_called);
    RUN_TEST(test_timestamp_hook_called_each_log);
    RUN_TEST(test_timestamp_value_appears_in_output);

    /* Overflow */
    RUN_TEST(test_overflow_output_not_exceeds_buf_size);
    RUN_TEST(test_overflow_output_ends_with_crlf);

    /* Mutex */
    RUN_TEST(test_mutex_lock_before_callback);
    RUN_TEST(test_mutex_unlock_after_callback);
    RUN_TEST(test_mutex_lock_unlock_called_once_per_log);

    /* Context */
    RUN_TEST(test_ctx_null_passed_to_callback);
    RUN_TEST(test_ctx_pointer_passed_to_callback);

    /* Runtime toggle */
    RUN_TEST(test_enabled_by_default);
    RUN_TEST(test_is_enabled_reflects_state);
    RUN_TEST(test_disabled_suppresses_output);
    RUN_TEST(test_reenabled_resumes_output);
    RUN_TEST(test_disabled_does_not_take_mutex);

    return UNITY_END();
}