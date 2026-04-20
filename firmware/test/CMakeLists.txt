# firmware/test/CMakeLists.txt Тестовая прошивка — входной контроль платы на
# производстве

set(TARGET_NAME firmware_test)

add_executable(
  ${TARGET_NAME}
  src/main.c
  src/cli.c
  src/protocol.c
  src/test_runner.c
  src/tests/test_sdram.c
  ${BSP_GENERATED}/clock_config.c
  ${BSP_STARTUP_FILE}
  ${BSP_SYSCALLS_FILE})

target_include_directories(firmware_test PRIVATE src/)

target_compile_definitions(
  ${TARGET_NAME} PRIVATE BSP_UART_HOST_RX_BUFFER_SIZE=512 BOARD_MPU_SDRAM=1)
#
# -----------------------------------------------------------------------------
# Зависимости — только то что нужно для входного контроля bsp_board транзитивно
# даёт: sdk_device, sdk_clock, sdk_common, CPU_MIMXRT1052CVJ5B, XIP_* дефайны
# -----------------------------------------------------------------------------
target_link_libraries(${TARGET_NAME} PRIVATE bsp_board bsp_led bsp_tick
                                             bsp_boot_xip bsp_usb_cdc bsp_sdram)

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
  -Wl,--defsym=__stack_size__=0x1000
  -Wl,--defsym=__heap_size__=0x1000
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
