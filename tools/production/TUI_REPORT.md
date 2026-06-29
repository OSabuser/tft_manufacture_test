# TUI Service Tool замечания

## Прочие замечания

После внедрения в firmware/test/CMakeLists.txt:

```cmake
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

```

сборка начала валиться с ошибками:

```bash
Executing task: just build::hab-firmware-test-debug 

cmake --preset Debug
Preset CMake variables:

  CMAKE_BUILD_TYPE="Debug"
  CMAKE_EXPORT_COMPILE_COMMANDS="ON"
  CMAKE_TOOLCHAIN_FILE:FILEPATH="/workspace/cmake/toolchain_arm.cmake"
  SEGGER_RTT_ENABLED="OFF"
  UNITY_TESTING_ENABLED="OFF"

-- ARM Toolchain: /opt/arm-toolchain
-- Build type: Debug
-- ==== Included external libraries ====
-- SEGGER RTT ❎
-- Unity ❎
-- FFF ❎
-- ==== ---------------- ====
-- Configuring done
-- Generating done
-- Build files have been written to: /workspace/build/Debug
cmake --build --preset firmware-test-debug
[108/108] Linking C executable firmware_test.elf
FAILED: firmware_test.elf 
: && /opt/arm-toolchain/bin/arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -ffreestanding -O0 -g3 -gdwarf-4 --specs=nano.specs -Wl,--no-warn-rwx-segments    -Wl,--gc-sections -Wl,--print-memory-usage -Wl,-Map=/workspace/build/Debug/firmware_test.map -Wl,--defsym=__stack_size__=0x2000 -Wl,--defsym=__heap_size__=0x2000 -T/workspace/firmware/test/cmake/linker/MIMXRT1052xxxxx_flexspi_nor_sdram.ld firmware/test/CMakeFiles/firmware_test.dir/src/main.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/cli.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/protocol.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/test_runner.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/tests/test_opto.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/tests/test_sdram.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/tests/test_can.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/tests/test_mqs.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/tests/test_qspi.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/tests/test_usd.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/tests/test_display.c.obj firmware/test/CMakeFiles/firmware_test.dir/src/tests/test_buttons.c.obj firmware/test/CMakeFiles/firmware_test.dir/__/__/bsp/generated/clock_config.c.obj firmware/test/CMakeFiles/firmware_test.dir/__/__/bsp/generated/startup/startup_MIMXRT1052.S.obj firmware/test/CMakeFiles/firmware_test.dir/__/__/bsp/generated/syscalls.c.obj -o firmware_test.elf  bsp/libbsp_board.a  bsp/can/libbsp_can.a  bsp/led/libbsp_led.a  bsp/button/libbsp_button.a  bsp/display/libbsp_display.a  bsp/tick/libbsp_tick.a  bsp/usb_cdc/libbsp_usb_cdc.a  bsp/provisioning/libbsp_provisioning.a  bsp/sdram/libbsp_sdram.a  bsp/qspi_flash/libbsp_qspi_flash.a  bsp/uart_host/libbsp_uart_host.a  bsp/opto/libbsp_opto.a  bsp/mqs/libbsp_mqs.a  bsp/sd/libbsp_sd.a  firmware/test/fatfs/libfirmware_test_fatfs.a  sdk/libsdk_flexcan.a  sdk/libsdk_elcdif.a  sdk/libsdk_usb_device_ehci.a  sdk/libsdk_usb_phy.a  sdk/libsdk_semc.a  sdk/libsdk_flexspi.a  utils/libutils.a  bsp/tick/libbsp_tick.a  sdk/libsdk_gpio.a  sdk/libsdk_sai_edma.a  sdk/libsdk_sai.a  sdk/libsdk_edma.a  sdk/libsdk_dmamux.a  sdk/libsdk_pwm.a  sdk/libsdk_xbara.a  bsp/sd/libbsp_sd.a  bsp/libbsp_sdmmc_config.a  bsp/libbsp_board.a  sdk/libsdk_lpuart.a  sdk/libsdk_sdmmc_sd.a  sdk/libsdk_osa_bm.a  sdk/libsdk_usdhc.a  sdk/libsdk_clock.a  sdk/libsdk_common.a  sdk/libsdk_cache.a  sdk/libsdk_device.a && cd /workspace/build/Debug/firmware/test && /opt/arm-toolchain/bin/arm-none-eabi-objcopy -O binary /workspace/build/Debug/firmware_test.elf /workspace/build/Debug/firmware_test.bin && /opt/arm-toolchain/bin/arm-none-eabi-size /workspace/build/Debug/firmware_test.elf
/opt/arm-gnu-toolchain-13.3.rel1-aarch64-arm-none-eabi/bin/../lib/gcc/arm-none-eabi/13.3.1/../../../../arm-none-eabi/bin/ld: cannot open linker script file /workspace/firmware/test/cmake/linker/MIMXRT1052xxxxx_flexspi_nor_sdram.ld: No such file or directory
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
error: Recipe `build-firmware-test-debug` failed on line 38 with exit code 1

 *  The terminal process "/bin/bash '-c', 'just build::hab-firmware-test-debug'" terminated with exit code: 1. 
 *  Terminal will be reused by tasks, press any key to close it. 
```

## Экран Waiting 

1. Вне зависимости от масштаба окна прямоугольник с названием программы TFT Indicator Board Service Tool всегда находится в левом верхнем углу и выглядит неуместным. Предлагаю для всех экранов для унификации ввести цветную рамку фиксированного размера 640x480/1280x1024, чтобы вне зависимости от размера окна мы всегда видели одно и то же. Если бы еще можно было запретить делать Maximize для конкретного окна - прекрасно. Мне нравится когда TUI приложение ограничено окном - рамкой. В ratatui-rust много похожих приложений. Например [binsider](https://github.com/orhun/binsider)

## Экран Flasher

1. progress bar с процентами выполнения: анимация работает даже в случае если ничего не выполняется
2. progress bar с процентами выполнения: при выполнении операций Cheap Erase и Прошить проценты не меняются, просто продолжается анимация. Только в конце операции появляется `done 100%` ниже progress Bar

Надо тщательно продумать логику работы прогресс бара. Нужно запускать его только при начале операций erase/прошить, можно не показывать проценты вообще. 

3. После выполнения операции Прошить приложение сразу перезапускает экран Waiting и мы опять попадаем в то же самое меню Flasher. Если сервисник хочет провести тесты - это неудобно. Ему нужно будет с помощью Ctrl+C  закрывать приложение, менять BootMode перезапускать плату и затем само приложение. Надо продумать здесь следующий механизм: если мы шьем firmware_test можно вывести экран с промптом а-ля - теперь перезапустите плату с другим режимом boot Mode, дать время секунд 40 если сервисник справится раньше - пусть жмет ОК, если он не успелприложениш просто перезапустится и в худщем случае мы опять попадем в Flasher. В общем тут надо подумать. При прошивке кастомного бинаря/production этот промпт не нужен (хотя можно оставить напоминание, что для запуска прошитого бинаря перезапустите плату с другим boot Mode. Надо в экране Flasher предусмотреть кнопку выхода из приложения.

4. Был странный вылет из приложения (экран Flasher) когда я рандомно выделял элементы на экране приложения и кликал :

```bash
production git:(dev) ✗ uv run python main.py
╭─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────── Traceback (most recent call last) ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╮
│ /Users/von_akimow/Desktop/TFT_ENV/tft_manufacture_test/tools/production/.venv/lib/python3.14/site-packages/textual/app.py:4082 in on_event                                                                                                                                              │
│                                                                                                                                                                                                                                                                                         │
│   4079 │   │   │   │   │   │   # Shouldn't occur, since at the very least this will find the Sc                                                                                                                                                                                         │
│   4080 │   │   │   │   │   │   self._mouse_down_widget = None                                                                                                                                                                                                                           │
│   4081 │   │   │   │                                                                                                                                                                                                                                                                    │
│ ❱ 4082 │   │   │   │   self.screen._forward_event(event)                                                                                                                                                                                                                                │
│   4083 │   │   │   │                                                                                                                                                                                                                                                                    │
│   4084 │   │   │   │   # If a MouseUp occurs at the same widget as a MouseDown, then we should                                                                                                                                                                                          │
│   4085 │   │   │   │   # consider it a click, and produce a Click event.                                                                                                                                                                                                                │
│                                                                                                                                                                                                                                                                                         │
│ ╭───────────────────────────────────────────────────────────────────────────────────────────────────── locals ─────────────────────────────────────────────────────────────────────────────────────────────────────╮                                                                    │
│ │ event = MouseMove(None, x=0, y=34, pointer_x=0.0, pointer_y=34.0, delta_x=-9, delta_y=-3, button=1, style=Style(bgcolor=Color('#121212', ColorType.TRUECOLOR, triplet=ColorTriplet(red=18, green=18, blue=18)))) │                                                                    │
│ │  self = ServiceApp(title='TFT Board Service Tool', classes={'-dark-mode'}, pseudo_classes={'dark', 'focus'})                                                                                                     │                                                                    │
│ ╰──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╯                                                                    │
│                                                                                                                                                                                                                                                                                         │
│ /Users/von_akimow/Desktop/TFT_ENV/tft_manufacture_test/tools/production/.venv/lib/python3.14/site-packages/textual/screen.py:1841 in _forward_event                                                                                                                                     │
│                                                                                                                                                                                                                                                                                         │
│   1838 │   │   │   │   │   if select_offset is not None:                                                                                                                                                                                                                                │
│   1839 │   │   │   │   │   │   content_widget = select_widget                                                                                                                                                                                                                           │
│   1840 │   │   │   │   │   │   content_offset = select_offset                                                                                                                                                                                                                           │
│ ❱ 1841 │   │   │   │   │   │   assert isinstance(content_widget.parent, Widget)                                                                                                                                                                                                         │
│   1842 │   │   │   │   │   │   container = content_widget.parent                                                                                                                                                                                                                        │
│   1843 │   │   │   │   │   else:                                                                                                                                                                                                                                                        │
│   1844 │   │   │   │   │   │   content_widget = None                                                                                                                                                                                                                                    │
│                                                                                                                                                                                                                                                                                         │
│ ╭───────────────────────────────────────────────────────────────────────────────────────────────────────── locals ──────────────────────────────────────────────────────────────────────────────────────────────────────────╮                                                           │
│ │ content_offset = Offset(x=0, y=33)                                                                                                                                                                                        │                                                           │
│ │ content_widget = FlashScreen()                                                                                                                                                                                            │                                                           │
│ │          event = MouseMove(None, x=0, y=34, pointer_x=0.0, pointer_y=34.0, delta_x=-9, delta_y=-3, button=1, style=Style(bgcolor=Color('#121212', ColorType.TRUECOLOR, triplet=ColorTriplet(red=18, green=18, blue=18)))) │                                                           │
│ │  select_offset = Offset(x=0, y=33)                                                                                                                                                                                        │                                                           │
│ │  select_widget = FlashScreen()                                                                                                                                                                                            │                                                           │
│ │           self = FlashScreen()                                                                                                                                                                                            │                                                           │
│ ╰───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╯                                                           │
╰─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╯
AssertionError
```

## Экран ConfirmPanel

1. Изначально все тесты из списка можно сделать некактивными
2. Помимо версии fw давай также будем выводить в тайтле UID камня
3. В поле результаты можно сделать симпатичную таблицу с двумя столбцами: название теста/ результат
4. Аналогично экрану Flasher: прогресс бар постоянно активен, даже в режиме простоя
5. Также можно предусмотреть кнопку выхода из приложения (аналогично Flasher)