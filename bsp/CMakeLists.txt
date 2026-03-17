if(BUILD_TESTS_HOST)
  return()
endif()

# -----------------------------------------------------------------------------
# bsp_board — стартап, сгенерированные файлы Config Tools, инициализация платы
# -----------------------------------------------------------------------------

add_library(
  bsp_board STATIC
  generated/board.c generated/pin_mux.c generated/clock_config.c
  generated/startup/startup_MIMXRT1052.S)

add_subdirectory(common)
add_subdirectory(led)
add_subdirectory(tick)
add_subdirectory(uart_host)

# Подавляем предупреждения при компиляции собственных .c файлов библиотеки
target_compile_options(bsp_board PRIVATE -w)

# SYSTEM подавляет предупреждения для всех внешних потребителей
target_include_directories(bsp_board SYSTEM PUBLIC generated)

# Дефайны PUBLIC — транзитивно подтянутся во все firmware таргеты
target_compile_definitions(
  bsp_board PUBLIC CPU_MIMXRT1052CVJ5B SKIP_SYSCLK_INIT XIP_EXTERNAL_FLASH=1
                   XIP_BOOT_HEADER_ENABLE=1 XIP_BOOT_HEADER_DCD_ENABLE=1)

# sdk_device PUBLIC — транзитивно подтянется во все firmware таргеты
target_link_libraries(bsp_board PUBLIC sdk_device sdk_clock sdk_common
                                       sdk_lpuart)

# Выбор boot стратегии (определяется в CMakePresets в зависимости от собираемого
# target)

# TODO: длякаждого приложения свои дефайны # Сценарий A: XIP — код исполняется
# прямо из Flash add_library(bsp_boot_xip INTERFACE) target_compile_definitions(
# bsp_boot_xip INTERFACE XIP_EXTERNAL_FLASH=1 XIP_BOOT_HEADER_ENABLE=1
# XIP_BOOT_HEADER_DCD_ENABLE=1)

# # Сценарий B: копирование в ITCM (для bootloader или критичного кода)
# add_library(bsp_boot_itcm INTERFACE) target_compile_definitions( bsp_boot_itcm
# INTERFACE XIP_BOOT_HEADER_ENABLE=1 XIP_BOOT_HEADER_DCD_ENABLE=1 #
# XIP_EXTERNAL_FLASH намеренно отсутствует )

# # Сценарий C: копирование в SDRAM (для app с графикой)
# add_library(bsp_boot_sdram INTERFACE) target_compile_definitions(
# bsp_boot_sdram INTERFACE XIP_BOOT_HEADER_ENABLE=1
# XIP_BOOT_HEADER_DCD_ENABLE=1)
