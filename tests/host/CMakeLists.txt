# tests/host/CMakeLists.txt

# -----------------------------------------------------------------------------
# Вспомогательная функция добавления host-теста.
#
# Использование: add_host_test( NAME    <имя теста> SOURCES <тест.c> [<модуль.c>
# ...]   # исходники теста + тестируемый код INCLUDES <dir> [<dir> ...] #
# дополнительные include-пути (опц.) MOCKS   <dir> [<dir> ...]           # папки
# со stub-хедерами (опц.) )
#
# Каждый тест — отдельный исполняемый файл. CTest запускает их все.
#
# Соглашения: - lib_external подключается всегда (Unity + fff). - INCLUDES и
# MOCKS — явные: каждый тест сам декларирует свои зависимости. Нет скрытых
# глобальных include-путей, специфичных для одного модуля.
# -----------------------------------------------------------------------------
function(add_host_test)
  cmake_parse_arguments(ARG "" "NAME" "SOURCES;INCLUDES;MOCKS" ${ARGN})

  add_executable(${ARG_NAME} ${ARG_SOURCES})

  target_link_libraries(${ARG_NAME} PRIVATE lib_external)

  # Явные include-пути, специфичные для теста
  foreach(inc_dir IN LISTS ARG_INCLUDES)
    target_include_directories(${ARG_NAME} PRIVATE ${inc_dir})
  endforeach()

  # Stub-хедеры для моков (PRIVATE — не торчат в других таргетах)
  foreach(mock_dir IN LISTS ARG_MOCKS)
    target_include_directories(${ARG_NAME} PRIVATE ${mock_dir})
  endforeach()

  add_test(
    NAME ${ARG_NAME}
    COMMAND ${ARG_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endfunction()

# =============================================================================
# Host тесты
# =============================================================================

set(BSP_MOCKS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mocks)

# -----------------------------------------------------------------------------
# bsp_led
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_bsp_led
  SOURCES
  led/test_bsp_led.c
  ${PROJECT_SOURCE_DIR}/bsp/led/src/led.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/bsp/led/include # для #include "bsp/led.h"
  MOCKS
  ${BSP_MOCKS_DIR} # fsl_gpio.h, pin_mux.h, board.h
)

# -----------------------------------------------------------------------------
# utils/ring_buffer
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_ring_buffer
  SOURCES
  ring_buffer/test_ring_buffer.c
  ${PROJECT_SOURCE_DIR}/utils/ring_buffer/ring_buffer.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/utils # для #include "ring_buffer/ring_buffer.h"
)

# -----------------------------------------------------------------------------
# utils/log
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_log
  SOURCES
  log/test_log.c
  ${PROJECT_SOURCE_DIR}/utils/log/log.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/utils # для "log/log.h"
)
# LOG_LEVEL=5 — все уровни активны, тесты уровней D и V не вырезаются
target_compile_definitions(test_log PRIVATE LOG_LEVEL=5)
# -----------------------------------------------------------------------------
# Тест таймаут-паттерна, используемого в bsp/tick
# -----------------------------------------------------------------------------
add_host_test(
  NAME test_timeout_pattern SOURCES timeout/test_timeout_pattern.c
  # нет INCLUDES и MOCKS — чистая математика, нет зависимостей
)

# -----------------------------------------------------------------------------
# Тест кода, использующего bsp_uart_host через мок
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  uart_host_mock_example
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/uart_host/test_uart_host.c
  ${CMAKE_SOURCE_DIR}/bsp/uart_host/mocks/uart_host_mock.c
  # Если тестируете "protocol.c" который использует uart_host:
  # ${CMAKE_SOURCE_DIR}/bsp/protocol/src/protocol.c
  INCLUDES
  ${CMAKE_SOURCE_DIR}/bsp/uart_host/include # bsp/uart_host.h
  ${CMAKE_SOURCE_DIR}/bsp/uart_host/mocks # uart_host_mock.h
  ${CMAKE_SOURCE_DIR}/bsp/common/include # bsp/status.h
  # ${CMAKE_SOURCE_DIR}/bsp/protocol/include  # protocol.h
  MOCKS
  ${CMAKE_SOURCE_DIR}/bsp/uart_host/mocks/uart_host_mock.c)

# -----------------------------------------------------------------------------
# Тест кода, использующего bsp_opto через мок
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_bsp_opto
  SOURCES
  opto/test_bsp_opto.c
  ${PROJECT_SOURCE_DIR}/bsp/opto/src/opto.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/bsp/opto/include
  ${PROJECT_SOURCE_DIR}/bsp/common/include
  MOCKS
  ${BSP_MOCKS_DIR})

# -----------------------------------------------------------------------------
# Тест логики bsp_can (категория B — SDK мокается через fff в тест-файле)
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_bsp_can
  SOURCES
  can/test_bsp_can.c
  ${CMAKE_SOURCE_DIR}/bsp/can/src/can.c
  ${CMAKE_SOURCE_DIR}/utils/ring_buffer/ring_buffer.c
  INCLUDES
  ${CMAKE_SOURCE_DIR}/bsp/can/include
  ${CMAKE_SOURCE_DIR}/bsp/common/include
  ${CMAKE_SOURCE_DIR}/utils/
  MOCKS
  ${BSP_MOCKS_DIR})

# -----------------------------------------------------------------------------
# Тест логики bsp_button (категория B — SDK мокается через fff в тест-файле)
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_bsp_button
  SOURCES
  button/test_bsp_button.c
  ${CMAKE_SOURCE_DIR}/bsp/button/src/button.c
  INCLUDES
  ${CMAKE_SOURCE_DIR}/bsp/button/include
  ${CMAKE_SOURCE_DIR}/bsp/common/include
  MOCKS
  ${BSP_MOCKS_DIR})

# -----------------------------------------------------------------------------
# Тест логики cli
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_cli
  SOURCES
  cli/test_cli.c
  ${CMAKE_SOURCE_DIR}/firmware/test/src/cli.c
  INCLUDES
  ${CMAKE_SOURCE_DIR}/firmware/test/src/
  ${CMAKE_SOURCE_DIR}/bsp/common/include
  ${CMAKE_SOURCE_DIR}/bsp/provisioning/include
  MOCKS
  ${BSP_MOCKS_DIR})

# -----------------------------------------------------------------------------
# utils/prio_queue
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_prio_queue
  SOURCES
  prio_queue/test_prio_queue.c
  ${PROJECT_SOURCE_DIR}/utils/prio_queue/prio_queue.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/utils)

# -----------------------------------------------------------------------------
# firmware/test/src/protocol.c
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_protocol
  SOURCES
  protocol/test_protocol.c
  ${PROJECT_SOURCE_DIR}/firmware/test/src/protocol.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/firmware/test/src
  MOCKS
  ${BSP_MOCKS_DIR})

# -----------------------------------------------------------------------------
# firmware_test: test_runner — реестр тестов и state machine Категория B:
# protocol_send_* + bsp_tick + bsp_usb_cdc + cli_process мокируются.
# Компилируется с -DUNIT_TEST для подстановки реестра из теста.
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_firmware_runner
  SOURCES
  runner/test_firmware_runner.c
  ${PROJECT_SOURCE_DIR}/firmware/test/src/test_runner.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/firmware/test/src
  MOCKS
  ${BSP_MOCKS_DIR})

target_compile_definitions(test_firmware_runner PRIVATE UNIT_TEST)

# -----------------------------------------------------------------------------
# bootutil (MCUboot Direct-XIP) — выбор слота. Реальный bootutil + TinyCrypt
# поверх fake_flash_map_backend.c (in-memory буфер вместо bsp_qspi_flash).
# См. firmware/bootloader/PLAN.md, Фаза 2.
# -----------------------------------------------------------------------------
include(${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/bootutil_sources.cmake)

add_host_test(
  NAME
  test_mcuboot_boot_select
  SOURCES
  mcuboot_port/test_boot_select.c
  mcuboot_port/fake_flash_map_backend.c
  mcuboot_port/host_link_shims.c
  ${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/keys.c
  ${MCUBOOT_BOOTUTIL_SOURCES}
  INCLUDES
  ${MCUBOOT_BOOTUTIL_INCLUDES}
  ${CMAKE_CURRENT_SOURCE_DIR}/mcuboot_port)

target_compile_definitions(
  test_mcuboot_boot_select
  PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/mcuboot_port/fixtures")

target_compile_options(test_mcuboot_boot_select PRIVATE -w)

# -----------------------------------------------------------------------------
# update_policy — чистая логика решения install/skip + целевой слот (Фаза 3,
# SD-путь установки). Нужен только заголовок bootutil/image.h (struct
# image_version) — без линковки самого bootutil, но fault_injection_hardening.h
# (включается из image.h) требует mcuboot_config/mcuboot_config.h в include-
# путях, поэтому переиспользуем MCUBOOT_BOOTUTIL_INCLUDES целиком (уже задан
# выше через bootutil_sources.cmake).
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_update_policy
  SOURCES
  update_policy/test_update_policy.c
  ${PROJECT_SOURCE_DIR}/firmware/bootloader/src/update_policy.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/firmware/bootloader/src
  ${MCUBOOT_BOOTUTIL_INCLUDES})

# -----------------------------------------------------------------------------
# recovery — чистая логика решения "что делать с этой попыткой загрузки"
# (Фаза 6, разбивка 6a). Переиспользует update_policy_slot_state_t/
# image_version_compare() из update_policy.c — тот же набор INCLUDES.
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_recovery
  SOURCES
  recovery/test_recovery.c
  ${PROJECT_SOURCE_DIR}/firmware/bootloader/src/recovery.c
  ${PROJECT_SOURCE_DIR}/firmware/bootloader/src/update_policy.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/firmware/bootloader/src
  ${MCUBOOT_BOOTUTIL_INCLUDES})

# -----------------------------------------------------------------------------
# slot_version — read-only пик версии слота через bootutil_img_validate()
# (Фаза 3). Реальный bootutil + TinyCrypt поверх fake_flash_map_backend.c —
# переиспользует те же фикстуры, что и test_mcuboot_boot_select (Фаза 2).
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_slot_version
  SOURCES
  slot_version/test_slot_version.c
  ${PROJECT_SOURCE_DIR}/firmware/bootloader/src/slot_version.c
  mcuboot_port/fake_flash_map_backend.c
  mcuboot_port/host_link_shims.c
  ${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/keys.c
  ${MCUBOOT_BOOTUTIL_SOURCES}
  INCLUDES
  ${PROJECT_SOURCE_DIR}/firmware/bootloader/src
  ${MCUBOOT_BOOTUTIL_INCLUDES}
  ${CMAKE_CURRENT_SOURCE_DIR}/mcuboot_port)

target_compile_definitions(
  test_slot_version PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/mcuboot_port/fixtures")

target_compile_options(test_slot_version PRIVATE -w)

# slot_version.c зовёт FIH_CALL(bootutil_img_validate, ...) напрямую — тот же
# баг clang 22.1.8 (Homebrew) с генерацией CFI-директив под ASan/UBSan, что
# уже обойдён для вендоренных файлов bootutil в bootutil_sources.cmake
# ("invalid CFI advance_loc expression"); здесь тот же обход для нашего
# файла, единственного, где эта макро-развёртка встречается вне bootutil.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/firmware/bootloader/src/slot_version.c
    PROPERTIES COMPILE_OPTIONS "-fno-sanitize=address,undefined")
endif()
