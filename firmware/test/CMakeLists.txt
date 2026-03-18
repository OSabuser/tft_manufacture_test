# firmware/test/CMakeLists.txt Тестовая прошивка — входной контроль платы на
# производстве

add_executable(firmware_test)
set(BSP_GENERATED ${CMAKE_SOURCE_DIR}/bsp/generated)
target_sources(
  firmware_test
  PRIVATE main.c ${PROJECT_SOURCE_DIR}/bsp/generated/syscalls.c
          ${BSP_GENERATED}/clock_config.c
          ${BSP_GENERATED}/startup/startup_MIMXRT1052.S)

target_compile_definitions(firmware_test
                           PRIVATE BSP_UART_HOST_RX_BUFFER_SIZE=512)
#
# -----------------------------------------------------------------------------
# Зависимости — только то что нужно для входного контроля bsp_board транзитивно
# даёт: sdk_device, sdk_clock, sdk_common, CPU_MIMXRT1052CVJ5B, XIP_* дефайны
# -----------------------------------------------------------------------------
target_link_libraries(
  firmware_test
  PRIVATE bsp_board bsp_led bsp_tick bsp_uart_host bsp_boot_ram
          lib_external # SEGGER RTT если включён через SEGGER_RTT_ENABLED
)

# -----------------------------------------------------------------------------
# Linker script
# -----------------------------------------------------------------------------
# --gc-sections        — удалять неиспользуемые секции (работает с
# -ffunction/data-sections) --print-memory-usage — выводить таблицу
# использования Flash/RAM после линковки -Map — генерировать map-файл для
# анализа размещения символов -T — линкерный скрипт с описанием карты памяти
# IMXRT1052
target_link_options(
  firmware_test PRIVATE -Wl,--gc-sections -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/firmware_test.map
  -T${PROJECT_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_ram.ld)

# -----------------------------------------------------------------------------
# Post-build: генерация .bin для прошивки через blhost
# -----------------------------------------------------------------------------
add_custom_command(
  TARGET firmware_test
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware_test>
          ${CMAKE_BINARY_DIR}/firmware_test.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:firmware_test>
          ${CMAKE_BINARY_DIR}/firmware_test.hex
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware_test>
  COMMENT "Generating firmware_test.bin")
