if(BUILD_TESTS_HOST)
  return()
endif()

# -----------------------------------------------------------------------------
# bsp_board — стартап, сгенерированные файлы Config Tools, инициализация платы
# -----------------------------------------------------------------------------

add_library(bsp_board STATIC board/board.c board/pin_mux.c board/clock_config.c
                             board/startup/startup_MIMXRT1052.S)

target_include_directories(bsp_board PUBLIC board)

# Подавляем предупреждения о macro redefined
set_source_files_properties(board/board.c board/pin_mux.c
                            PROPERTIES COMPILE_OPTIONS "-w")
# Дефайны PUBLIC — транзитивно подтянутся во все firmware таргеты TODO: для
# каждого приложения свои дефайны
target_compile_definitions(
  bsp_board PUBLIC CPU_MIMXRT1052CVJ5B SKIP_SYSCLK_INIT XIP_EXTERNAL_FLASH=1
                   XIP_BOOT_HEADER_ENABLE=1 XIP_BOOT_HEADER_DCD_ENABLE=1)

# Сценарий A: XIP — код исполняется прямо из Flash
add_library(bsp_boot_xip INTERFACE)
target_compile_definitions(
  bsp_boot_xip INTERFACE XIP_EXTERNAL_FLASH=1 XIP_BOOT_HEADER_ENABLE=1
                         XIP_BOOT_HEADER_DCD_ENABLE=1)

# Сценарий B: копирование в ITCM (для bootloader или критичного кода)
add_library(bsp_boot_itcm INTERFACE)
target_compile_definitions(
  bsp_boot_itcm INTERFACE XIP_BOOT_HEADER_ENABLE=1 XIP_BOOT_HEADER_DCD_ENABLE=1
                          # XIP_EXTERNAL_FLASH намеренно отсутствует
)

# Сценарий C: копирование в SDRAM (для app с графикой)
add_library(bsp_boot_sdram INTERFACE)
target_compile_definitions(
  bsp_boot_sdram INTERFACE XIP_BOOT_HEADER_ENABLE=1
                           XIP_BOOT_HEADER_DCD_ENABLE=1)

# sdk_device PUBLIC — транзитивно подтянется во все firmware таргеты
target_link_libraries(bsp_board PUBLIC sdk_device sdk_clock sdk_common)
