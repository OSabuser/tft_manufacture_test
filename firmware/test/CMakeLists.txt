# firmware/test/CMakeLists.txt Тестовая прошивка — входной контроль платы на
# производстве

add_executable(firmware_test main.c)

# -----------------------------------------------------------------------------
# Зависимости — только то что нужно для входного контроля bsp_board транзитивно
# даёт: sdk_device, sdk_clock, sdk_common, CPU_MIMXRT1052CVJ5B, XIP_* дефайны
# -----------------------------------------------------------------------------
target_link_libraries(
  firmware_test
  PRIVATE bsp_board
          # SDK драйверы периферии — добавляй по мере написания тестов
          sdk_lpuart # UART тест
          sdk_flexcan # CAN тест
          sdk_gpio # GPIO тест (кнопки, светодиоды, входы)
          sdk_usdhc # uSD тест
          sdk_semc # SDRAM тест
          # Опциональные библиотеки
          lib_external # SEGGER RTT если включён через SEGGER_RTT_ENABLED
)

# -----------------------------------------------------------------------------
# Linker script
# -----------------------------------------------------------------------------
# --gc-sections — убирает неиспользуемые секции кода и данных. Работает в паре с
# -ffunction-sections -fdata-sections у компилятора (каждая функция в отдельной
# секции — линкер выбрасывает ненужные).
target_link_options(
  firmware_test
  PRIVATE
  -T${PROJECT_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_flexspi_nor.ld
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_BINARY_DIR}/firmware_test.map
  -Wl,--print-memory-usage)

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
