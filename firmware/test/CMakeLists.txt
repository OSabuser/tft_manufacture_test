# firmware/test/CMakeLists.txt Тестовая прошивка — входной контроль платы на
# производстве

add_executable(firmware_test)

target_sources(firmware_test PRIVATE main.c
                                     ${PROJECT_SOURCE_DIR}/bsp/board/syscalls.c)

# -----------------------------------------------------------------------------
# Зависимости — только то что нужно для входного контроля bsp_board транзитивно
# даёт: sdk_device, sdk_clock, sdk_common, CPU_MIMXRT1052CVJ5B, XIP_* дефайны
# -----------------------------------------------------------------------------
target_link_libraries(
  firmware_test
  PRIVATE bsp_board
          bsp_boot_xip
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
# --gc-sections        — удалять неиспользуемые секции (работает с
# -ffunction/data-sections) --print-memory-usage — выводить таблицу
# использования Flash/RAM после линковки -Map                 — генерировать
# map-файл для анализа размещения символов -T                   — линкерный
# скрипт с описанием карты памяти IMXRT1052
target_link_options(
  firmware_test PRIVATE -Wl,--gc-sections -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/firmware_test.map
  -T${PROJECT_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_flexspi_nor.ld)

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
