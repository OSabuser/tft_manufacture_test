/**
 * @file  test_settings_codec.c
 * @brief Host-тесты settings_codec.c (сериализация ядра настроек). См. README.md.
 */

#include "settings_codec.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_page_is_one_sector(void)
{
    TEST_ASSERT_EQUAL_UINT(BSP_QSPI_SECTOR_SIZE, sizeof(settings_page_t));
}

static void test_defaults_sane(void)
{
    const settings_t D = settings_defaults();
    TEST_ASSERT_EQUAL_UINT8(0U, D.device.protocol_id);
    TEST_ASSERT_EQUAL_UINT8(1U, D.device.log_level); /* «Инфо» из коробки */
    TEST_ASSERT_EQUAL_UINT8(2U, D.user.sound_volume_idx);
    TEST_ASSERT_EQUAL_UINT8(1U, D.user.music_volume_idx);
    TEST_ASSERT_EQUAL_UINT16(0U, D.user.max_load_kg);
    TEST_ASSERT_EQUAL_UINT8(0U, D.user.proto_slice[0]);
}

static void test_serialize_sets_magic_version(void)
{
    settings_t      in = settings_defaults();
    settings_page_t page;
    settings_serialize(&in, &page);
    TEST_ASSERT_EQUAL_HEX32(SETTINGS_MAGIC, page.magic);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_VERSION, page.version);
}

static void test_roundtrip_preserves_fields(void)
{
    settings_t in           = settings_defaults();
    in.user.max_load_kg     = 1000U;
    in.user.max_cap_persons = 8U;
    in.user.year_production  = 25U;
    in.user.proto_slice[0]  = 7U;
    in.device.log_level     = 0U;
    (void) snprintf(in.user.serial, SETTINGS_SERIAL_LEN, "AB1234");

    settings_page_t page;
    settings_serialize(&in, &page);

    settings_t out;
    TEST_ASSERT_TRUE(settings_deserialize(&page, &out));
    TEST_ASSERT_EQUAL_UINT16(1000U, out.user.max_load_kg);
    TEST_ASSERT_EQUAL_UINT8(8U, out.user.max_cap_persons);
    TEST_ASSERT_EQUAL_UINT8(25U, out.user.year_production);
    TEST_ASSERT_EQUAL_UINT8(7U, out.user.proto_slice[0]);
    TEST_ASSERT_EQUAL_UINT8(0U, out.device.log_level);
    TEST_ASSERT_EQUAL_STRING("AB1234", out.user.serial);
}

static void test_bad_magic_rejected(void)
{
    settings_t      in = settings_defaults();
    settings_page_t page;
    settings_serialize(&in, &page);
    page.magic ^= 0xFFFFFFFFU;

    settings_t out;
    TEST_ASSERT_FALSE(settings_deserialize(&page, &out));
}

static void test_bad_version_rejected(void)
{
    settings_t      in = settings_defaults();
    settings_page_t page;
    settings_serialize(&in, &page);
    page.version = (uint8_t) (SETTINGS_VERSION + 1U);

    settings_t out;
    TEST_ASSERT_FALSE(settings_deserialize(&page, &out));
}

static void test_bad_crc_rejected(void)
{
    settings_t      in = settings_defaults();
    settings_page_t page;
    settings_serialize(&in, &page);
    page.data.user.max_cap_persons ^= 0x01U;

    settings_t out;
    TEST_ASSERT_FALSE(settings_deserialize(&page, &out));
}

static void test_erased_page_is_invalid(void)
{
    settings_page_t page;
    memset(&page, 0xFFU, sizeof(page));

    settings_t out;
    TEST_ASSERT_FALSE(settings_deserialize(&page, &out));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_page_is_one_sector);
    RUN_TEST(test_defaults_sane);
    RUN_TEST(test_serialize_sets_magic_version);
    RUN_TEST(test_roundtrip_preserves_fields);
    RUN_TEST(test_bad_magic_rejected);
    RUN_TEST(test_bad_version_rejected);
    RUN_TEST(test_bad_crc_rejected);
    RUN_TEST(test_erased_page_is_invalid);

    return UNITY_END();
}
