/**
 * @file  test_slot_version.c
 * @brief Host-тесты slot_version_get() — read-only пик версии слота.
 *
 * Переиспользует фейковый flash-бэкенд и фикстуры
 * tests/host/mcuboot_port/ (Фаза 2) — реальный bootutil (hash+ECDSA-подпись)
 * поверх in-memory буфера вместо bsp_qspi_flash, те же подписанные imgtool
 * образы.
 */

#include "unity.h"

#include "fake_flash_map_backend.h"
#include "slot_version.h"

#include <stdio.h>
#include <string.h>

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined by CMake (see tests/host/CMakeLists.txt)"
#endif

static uint8_t g_s_fixture_buf[FAKE_FLASH_SLOT_SIZE];

static size_t load_fixture(const char *p_name)
{
    char path[256];
    (void) snprintf(path, sizeof(path), "%s/%s", FIXTURES_DIR, p_name);

    FILE *p_file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(p_file, path);

    size_t n = fread(g_s_fixture_buf, 1U, sizeof(g_s_fixture_buf), p_file);
    (void) fclose(p_file);

    TEST_ASSERT_EQUAL_UINT32(FAKE_FLASH_SLOT_SIZE, n);
    return n;
}

/* Версия зашита в первых байтах фикстуры (image_header) — читаем её
 * напрямую из буфера вместо того чтобы угадывать/дублировать число в тесте. */
static struct image_version fixture_header_version(void)
{
    struct image_header hdr;
    memcpy(&hdr, g_s_fixture_buf, sizeof(hdr));
    return hdr.ih_ver;
}

void setUp(void)
{
    fake_flash_reset();
}

void tearDown(void)
{
}

/* ── Валидный слот ─────────────────────────────────────────────────────── */

void test_valid_slot_returns_its_header_version(void)
{
    size_t len                        = load_fixture("valid_v1.bin");
    struct image_version expected_ver = fixture_header_version();

    fake_flash_write_slot(0, g_s_fixture_buf, len);

    struct image_version actual_ver;
    bool ok = slot_version_get(0U, &actual_ver);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(expected_ver.iv_major, actual_ver.iv_major);
    TEST_ASSERT_EQUAL_UINT8(expected_ver.iv_minor, actual_ver.iv_minor);
    TEST_ASSERT_EQUAL_UINT16(expected_ver.iv_revision, actual_ver.iv_revision);
}

/* ── Повреждённый слот ─────────────────────────────────────────────────── */

void test_corrupt_slot_returns_false(void)
{
    size_t len = load_fixture("corrupt_v1.bin");
    fake_flash_write_slot(1, g_s_fixture_buf, len);

    struct image_version ver;
    bool ok = slot_version_get(1U, &ver);

    TEST_ASSERT_FALSE(ok);
}

/* ── Пустой слот ───────────────────────────────────────────────────────── */

void test_empty_slot_returns_false(void)
{
    /* fake_flash_reset() в setUp уже оставил слот стёртым (0xFF) — magic
     * не совпадёт с IMAGE_MAGIC. */
    struct image_version ver;
    bool ok = slot_version_get(0U, &ver);

    TEST_ASSERT_FALSE(ok);
}

/* ── Неподтверждённый, но валидный образ ──────────────────────────────────
 *
 * Ключевая гарантия slot_version_get(): в отличие от boot_go(), статус
 * confirm/copy_done не проверяется и не изменяется — вызов идемпотентен,
 * безопасен сколько угодно раз и до первого boot_go() (см. slot_version.h).
 */

void test_unconfirmed_valid_image_still_reports_version(void)
{
    size_t len                        = load_fixture("valid_v2_unconfirmed.bin");
    struct image_version expected_ver = fixture_header_version();

    fake_flash_write_slot(0, g_s_fixture_buf, len);

    struct image_version actual_ver;
    TEST_ASSERT_TRUE(slot_version_get(0U, &actual_ver));
    TEST_ASSERT_EQUAL_UINT8(expected_ver.iv_major, actual_ver.iv_major);

    /* Повторный вызов — по-прежнему успешен, слот не "стёрт", в отличие от
     * повторного boot_go() (см. test_mcuboot_boot_select::
     * test_boot_go_reverts_unconfirmed_image, где второй boot_go() как раз
     * стирает такой же образ). */
    TEST_ASSERT_TRUE(slot_version_get(0U, &actual_ver));
}

/* ── Точка входа ───────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_valid_slot_returns_its_header_version);
    RUN_TEST(test_corrupt_slot_returns_false);
    RUN_TEST(test_empty_slot_returns_false);
    RUN_TEST(test_unconfirmed_valid_image_still_reports_version);

    return UNITY_END();
}
