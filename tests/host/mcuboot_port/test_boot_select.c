/**
 * @file  test_boot_select.c
 * @brief Host-тесты выбора слота bootutil (MCUboot Direct-XIP + Revert).
 *
 * Реальный bootutil (loader.c, image_validate.c, tlv.c, ...) + TinyCrypt
 * поверх fake_flash_map_backend.c (in-memory буфер вместо bsp_qspi_flash).
 * Фикстуры в fixtures/ подписаны настоящим imgtool тестовым ключом
 * MCUboot (root-ec-p256.pem, см. mcuboot_port/keys/bootloader_test_ecdsa_pub.c)
 * — тестируется реальная проверка подписи/версии/TLV, не мок.
 *
 * Сценарии — firmware/bootloader/PLAN.md, Фаза 2:
 *   1. Валиден только Slot A → выбран A.
 *   2. Оба валидны, версия Б выше → выбран Б.
 *   3. Slot Б повреждён (битый хэш/подпись) → игнорируется, выбран A.
 *   4. Оба слота пусты/невалидны → boot_go() возвращает ошибку (триггер
 *      top-level состояния "нет образа" из Фазы 3).
 *   5. Direct-XIP Revert: образ выбран, но ни разу не confirmed → при
 *      следующей загрузке bootutil стирает слот и boot_go() проваливается.
 */

#include "unity.h"

#include "fake_flash_map_backend.h"

#include "bootutil/bootutil.h"
#include "bootutil/fault_injection_hardening.h"

#include <stdio.h>
#include <string.h>

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined by CMake (see tests/host/CMakeLists.txt)"
#endif

static uint8_t g_s_fixture_buf[FAKE_FLASH_SLOT_SIZE];

/**
 * @brief Загрузить фикстур-файл в g_s_fixture_buf.
 * @return Число прочитанных байт.
 */
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

void setUp(void)
{
    fake_flash_reset();
}

void tearDown(void)
{
}

/* ── Сценарий 1 — валиден только Slot A ──────────────────────────────── */

void test_boot_go_slot_a_only_valid(void)
{
    fake_flash_write_slot(0, g_s_fixture_buf, load_fixture("valid_v1.bin"));

    struct boot_rsp rsp;
    fih_ret fih_rc = boot_go(&rsp);

    TEST_ASSERT_TRUE(FIH_EQ(fih_rc, FIH_SUCCESS));
    TEST_ASSERT_EQUAL_UINT32(0U, rsp.br_image_off);
}

/* ── Сценарий 2 — оба валидны, побеждает более новая версия ──────────── */

void test_boot_go_picks_higher_version(void)
{
    fake_flash_write_slot(0, g_s_fixture_buf, load_fixture("valid_v1.bin"));
    fake_flash_write_slot(1, g_s_fixture_buf, load_fixture("valid_v2.bin"));

    struct boot_rsp rsp;
    fih_ret fih_rc = boot_go(&rsp);

    TEST_ASSERT_TRUE(FIH_EQ(fih_rc, FIH_SUCCESS));
    TEST_ASSERT_EQUAL_UINT32(FAKE_FLASH_SLOT_SIZE, rsp.br_image_off);
}

/* ── Сценарий 3 — повреждённый Slot Б игнорируется ───────────────────── */

void test_boot_go_ignores_corrupted_slot(void)
{
    fake_flash_write_slot(0, g_s_fixture_buf, load_fixture("valid_v1.bin"));
    fake_flash_write_slot(1, g_s_fixture_buf, load_fixture("corrupt_v1.bin"));

    struct boot_rsp rsp;
    fih_ret fih_rc = boot_go(&rsp);

    TEST_ASSERT_TRUE(FIH_EQ(fih_rc, FIH_SUCCESS));
    TEST_ASSERT_EQUAL_UINT32(0U, rsp.br_image_off);
}

/* ── Сценарий 4 — оба слота пусты → нет загружаемого образа ──────────── */

void test_boot_go_no_valid_image(void)
{
    /* fake_flash_reset() в setUp уже оставил оба слота стёртыми (0xFF) */
    struct boot_rsp rsp;
    fih_ret fih_rc = boot_go(&rsp);

    TEST_ASSERT_FALSE(FIH_EQ(fih_rc, FIH_SUCCESS));
}

/* ── Сценарий 5 — Direct-XIP Revert: неподтверждённый образ стирается ── */

void test_boot_go_reverts_unconfirmed_image(void)
{
    fake_flash_write_slot(0, g_s_fixture_buf, load_fixture("valid_v2_unconfirmed.bin"));

    struct boot_rsp rsp;

    /* Первая загрузка: образ валиден, выбран, но boot_set_confirmed() никто
     * не вызвал (симулируем что tft_app не подтвердила себя). */
    fih_ret fih_rc = boot_go(&rsp);
    TEST_ASSERT_TRUE(FIH_EQ(fih_rc, FIH_SUCCESS));

    /* Вторая загрузка "после перезагрузки" — bootutil видит copy_done=SET,
     * image_ok не SET → стирает слот и не находит образ. */
    fih_rc = boot_go(&rsp);
    TEST_ASSERT_FALSE(FIH_EQ(fih_rc, FIH_SUCCESS));
}

/* ── Точка входа ───────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_boot_go_slot_a_only_valid);
    RUN_TEST(test_boot_go_picks_higher_version);
    RUN_TEST(test_boot_go_ignores_corrupted_slot);
    RUN_TEST(test_boot_go_no_valid_image);
    RUN_TEST(test_boot_go_reverts_unconfirmed_image);

    return UNITY_END();
}
