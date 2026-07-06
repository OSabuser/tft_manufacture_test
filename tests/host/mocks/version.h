#pragma once

/**
 * @file  version.h
 * @brief Stub version.h для host-тестов.
 *
 * Реальный version.h генерируется CMake (configure_file) из
 * firmware/test/src/version.h.in только при конфигурации ARM-таргета
 * firmware_test (BUILD_TESTS_TARGET=ON). В host-debug/host-release
 * firmware/test/CMakeLists.txt не подключается — generated/version.h
 * не существует. Значения ниже фиксированные, участвуют только
 * в сериализации протокола (session_start/version_response), не в логике.
 */

#define FIRMWARE_TEST_VERSION_MAJOR 0
#define FIRMWARE_TEST_VERSION_MINOR 0
#define FIRMWARE_TEST_VERSION_PATCH 0

#define FIRMWARE_TEST_VERSION_STR "0.0.0-host-test"