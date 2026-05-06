add_library(bsp_sd STATIC src/sd.c)

target_include_directories(bsp_sd PUBLIC include)

target_link_libraries(
  bsp_sd
  PUBLIC bsp_status # bsp_status_t
  PRIVATE bsp_sdmmc_config # BOARD_SD_Config, sdmmc_config.h тащит sdk_sdmmc_sd
                           # транзитивно
)
