# port_fatfs_sd — INTERFACE: не компилирует diskio_sd.c сам.
#
# Причина: diskio_sd.c включает diskio.h → ff.h → ffconf.h, который разный у
# firmware_test (bare-metal) и tft_app (FreeRTOS). Каждый бинарник компилирует
# diskio_sd.c в свой FatFS-таргет сам, используя PORT_FATFS_SD_SRC.

add_library(port_fatfs_sd INTERFACE)

target_include_directories(port_fatfs_sd INTERFACE include)

target_link_libraries(
  port_fatfs_sd
  INTERFACE sdk_fatfs_headers # ff.h, fsl_sd_disk.h + SDK_FATFS_*_SRC
            bsp_sd) # g_sd, bsp_sd_is_inserted()

set(PORT_FATFS_SD_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/diskio_sd.c
    CACHE INTERNAL
          "port_fatfs_sd: путь к diskio_sd.c (компилировать per-binary)")
