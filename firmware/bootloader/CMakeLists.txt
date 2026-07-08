# firmware/bootloader/CMakeLists.txt Загрузчик — A/Б обновление tft_app через
# microSD (Direct-XIP). Обновляется только через USB ROM + blhost / SWD.
cmake_minimum_required(VERSION 3.20)
project(
  bootloader
  VERSION 0.1.0
  LANGUAGES C ASM)

set(TARGET_NAME bootloader)

# Генерация version.h из шаблона
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

add_executable(
  ${TARGET_NAME}
  src/main.c
  src/cli.c
  src/protocol.c
  ${BSP_GENERATED}/clock_config.c
  ${BSP_STARTUP_FILE}
  ${BSP_SYSCALLS_FILE})

target_include_directories(${TARGET_NAME} PRIVATE src/)

target_include_directories(${TARGET_NAME}
                           PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_definitions(
  ${TARGET_NAME} PRIVATE __STARTUP_INITIALIZE_RAMFUNCTION __STARTUP_CLEAR_BSS
                         __STARTUP_INITIALIZE_NONCACHEDATA)

# -----------------------------------------------------------------------------
# Зависимости — минимум для Фазы 1 (bring-up + CDC). bsp_qspi_flash (доступ к
# слотам) и bsp_button (downgrade-override) добавятся в Фазах 2-3.
# -----------------------------------------------------------------------------
target_link_libraries(${TARGET_NAME} PRIVATE bsp_board bsp_led bsp_tick
                                             bsp_usb_cdc bsp_boot_xip_no_dcd)

# -----------------------------------------------------------------------------
# Linker script — вариант flexspi_nor с m_text, ограниченным бюджетом
# bootloader (256 KB, см. docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md). Без SDRAM
# (в отличие от firmware_test) — bootloader SDRAM не использует.
# -----------------------------------------------------------------------------
target_link_options(
  ${TARGET_NAME}
  PRIVATE
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/bootloader.map
  -Wl,--defsym=__stack_size__=0x1000
  -Wl,--defsym=__heap_size__=0x1000
  -T${CMAKE_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_bootloader_flexspi_nor.ld)

set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                ${CMAKE_BINARY_DIR})

# -----------------------------------------------------------------------------
# Post-build: генерация .bin для прошивки через blhost
# -----------------------------------------------------------------------------
add_custom_command(
  TARGET ${TARGET_NAME}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}>
          ${CMAKE_BINARY_DIR}/bootloader.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
  COMMENT "Generating bootloader.bin")
