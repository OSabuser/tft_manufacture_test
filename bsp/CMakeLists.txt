if(BUILD_TESTS_HOST)
  return()
endif()

# -----------------------------------------------------------------------------
# bsp_board — генерированные файлы Config Tools, инициализация платы
# -----------------------------------------------------------------------------

add_library(bsp_board STATIC generated/board.c generated/pin_mux.c)

add_subdirectory(common)
add_subdirectory(led)
add_subdirectory(tick)
add_subdirectory(uart_host)
add_subdirectory(opto)
add_subdirectory(can)
add_subdirectory(button)
add_subdirectory(display)
add_subdirectory(usb_cdc)
add_subdirectory(sdram)
add_subdirectory(qspi_flash)
add_subdirectory(sd)

# Подавляем предупреждения при компиляции собственных .c файлов библиотеки
target_compile_options(bsp_board PRIVATE -w)

# SYSTEM подавляет предупреждения для всех внешних потребителей
target_include_directories(bsp_board SYSTEM PUBLIC generated)

# Дефайны PUBLIC — транзитивно подтянутся во все firmware таргеты
target_compile_definitions(bsp_board PUBLIC CPU_MIMXRT1052CVJ5B)

# sdk_device PUBLIC — транзитивно подтянется во все firmware таргеты
target_link_libraries(bsp_board PUBLIC sdk_device sdk_clock sdk_common
                                       sdk_lpuart)

# -----------------------------------------------------------------------------
# bsp_sdmmc_config — board-level конфигурация SDMMC (BOARD_SD_Config и пр.)
# Отдельный таргет, чтобы sdk_sdmmc_sd не тянулся во все бинарники через
# bsp_board. Линкуется только теми, кто реально работает с SD: bsp_usd →
# bsp_sdmmc_config
# -----------------------------------------------------------------------------
add_library(bsp_sdmmc_config STATIC generated/sdmmc_config.c)

target_include_directories(bsp_sdmmc_config PUBLIC generated)

target_compile_definitions(bsp_sdmmc_config PUBLIC SD_ENABLED)

target_link_libraries(
  bsp_sdmmc_config
  PUBLIC bsp_board # board.h, pin_mux.h, clock_config.h, fsl_gpio.h
  PUBLIC sdk_sdmmc_sd # fsl_sd.h, fsl_sdmmc_host.h, sdmmchost_t
)

target_compile_options(bsp_sdmmc_config PRIVATE -w)

# ------------------------------------------------------------------
# Boot-стратегии — INTERFACE
# ------------------------------------------------------------------
add_library(bsp_boot_xip INTERFACE)
target_compile_definitions(
  bsp_boot_xip INTERFACE XIP_EXTERNAL_FLASH=1 XIP_BOOT_HEADER_ENABLE=1
                         XIP_BOOT_HEADER_DCD_ENABLE=1 SKIP_SYSCLK_INIT)

add_library(bsp_boot_ram INTERFACE)
target_compile_definitions(bsp_boot_ram INTERFACE SKIP_SYSCLK_INIT)
