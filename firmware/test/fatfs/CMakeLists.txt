# firmware_test_fatfs — FatFS, скомпилированный с bare-metal ffconf.h.
#
# ff.c, fsl_sd_disk.c, diskio_sd.c включают ff.h → ffconf.h. Все три
# компилируются здесь, чтобы видели один и тот же ffconf.h из
# ${CMAKE_CURRENT_SOURCE_DIR}/include.
#
# tft_app заведёт свой аналогичный таргет с FreeRTOS ffconf.h — эти два таргета
# никак не пересекаются.

add_library(
  firmware_test_fatfs STATIC
  ${SDK_FATFS_FF_SRC} # sdk/middleware/fatfs/source/ff.c
  ${SDK_FATFS_SD_DISK_SRC} # sdk/middleware/fatfs/source/fsl_sd_disk/fsl_sd_disk.c
  ${PORT_FATFS_SD_SRC} # port/fatfs/sd/src/diskio_sd.c
  src/diskio.c)

# include/ первым — ffconf.h отсюда должен перекрыть любой шаблонный
target_include_directories(
  firmware_test_fatfs
  PUBLIC include # ffconf.h, виден потребителям (bsp_usd, test_usd)
  PRIVATE src)

target_link_libraries(
  firmware_test_fatfs
  PUBLIC port_fatfs_sd # diskio_sd.h, ff.h, fsl_sd_disk.h, bsp_sd
  PRIVATE bsp_sdmmc_config) # SD_ENABLED транзитивно из bsp_sd, но явно для
                            # ясности

target_compile_options(firmware_test_fatfs PRIVATE -w)
