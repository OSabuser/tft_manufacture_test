# firmware/test/CMakeLists.txt Тестовая прошивка — входной контроль платы на
# производстве
cmake_minimum_required(VERSION 3.20)
project(
  firmware_test
  VERSION 0.0.1
  LANGUAGES C ASM)

set(TARGET_NAME firmware_test)

# Генерация version.h из шаблона
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

add_subdirectory(fatfs)

add_executable(
  ${TARGET_NAME}
  src/main.c
  src/cli.c
  src/protocol.c
  src/test_runner.c
  src/tests/test_opto.c
  src/tests/test_sdram.c
  src/tests/test_can.c
  src/tests/test_mqs.c
  src/tests/test_qspi.c
  src/tests/test_usd.c
  src/tests/test_display.c
  src/tests/test_buttons.c
  ${BSP_GENERATED}/clock_config.c
  ${BSP_STARTUP_FILE}
  ${BSP_SYSCALLS_FILE})

target_include_directories(firmware_test PRIVATE src/)

target_include_directories(firmware_test
                           PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# __STARTUP_INITIALIZE_RAMFUNCTION - очистка секции .ram_function и копирование
# туда данных из __ram_function_flash_start; __STARTUP_INITIALIZE_NONCACHEDATA -
# инициализация некешируемое секции нулями
target_compile_definitions(
  ${TARGET_NAME}
  PRIVATE BSP_UART_HOST_RX_BUFFER_SIZE=512 BOARD_MPU_SDRAM=1
          DISPLAY_TEST_TYPE=BSP_DISPLAY_TFT8 __STARTUP_INITIALIZE_RAMFUNCTION
          __STARTUP_CLEAR_BSS __STARTUP_INITIALIZE_NONCACHEDATA)
#
# -----------------------------------------------------------------------------
# Зависимости — только то что нужно для входного контроля bsp_board транзитивно
# даёт: sdk_device, sdk_clock, sdk_common, CPU_MIMXRT1052CVJ5B, XIP_* дефайны
# -----------------------------------------------------------------------------
target_link_libraries(
  ${TARGET_NAME}
  PRIVATE bsp_board
          bsp_can
          bsp_led
          bsp_button
          bsp_display
          bsp_tick
          bsp_boot_xip
          bsp_usb_cdc
          bsp_provisioning
          bsp_sdram
          bsp_qspi_flash
          bsp_uart_host
          bsp_opto
          bsp_mqs
          bsp_sd
          firmware_test_fatfs)

# -----------------------------------------------------------------------------
# Linker script
# -----------------------------------------------------------------------------
# --gc-sections        — удалять неиспользуемые секции (работает с
# -ffunction/data-sections) --print-memory-usage — выводить таблицу
# использования Flash/RAM после линковки -Map                 — генерировать
# map-файл для анализа размещения символов -T                   — линкерный
# скрипт с описанием карты памяти IMXRT1052
target_link_options(
  ${TARGET_NAME}
  PRIVATE
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/firmware_test.map
  -Wl,--defsym=__stack_size__=0x2000
  -Wl,--defsym=__heap_size__=0x2000
  -T${PROJECT_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_flexspi_nor_sdram.ld)

set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                ${CMAKE_BINARY_DIR})
# -----------------------------------------------------------------------------
# Post-build: генерация .bin для прошивки через blhost
# -----------------------------------------------------------------------------
add_custom_command(
  TARGET firmware_test
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware_test>
          ${CMAKE_BINARY_DIR}/firmware_test.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware_test>
  COMMENT "Generating firmware_test.bin")
