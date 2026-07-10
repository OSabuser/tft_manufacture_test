# bootloader_fatfs — FatFS, скомпилированный с bare-metal ffconf.h для
# bootloader.
#
# ff.c, fsl_sd_disk.c, diskio_sd.c включают ff.h → ffconf.h. Все три
# компилируются здесь, чтобы видели один и тот же ffconf.h из
# ${CMAKE_CURRENT_SOURCE_DIR}/include.
#
# Не шарить с firmware_test_fatfs: bootloader и firmware_test —
# взаимоисключающие прошивки одной платы (см. firmware/bootloader/PLAN.md,
# Фаза 3) — зависимость от таргета с именем "firmware_test" была бы неверной
# связью. tft_app заведёт свой аналогичный таргет с FreeRTOS ffconf.h.
#
# Отличие от firmware_test_fatfs: FF_FS_READONLY=1 — bootloader только читает
# TFT_APP.BIN, никогда не пишет на SD (см. include/ffconf.h).

add_library(
  bootloader_fatfs STATIC
  ${SDK_FATFS_FF_SRC} # sdk/middleware/fatfs/source/ff.c
  ${SDK_FATFS_SD_DISK_SRC} # sdk/middleware/fatfs/source/fsl_sd_disk/fsl_sd_disk.c
  ${PORT_FATFS_SD_SRC} # port/fatfs/sd/src/diskio_sd.c
  src/diskio.c)

# include/ первым — ffconf.h отсюда должен перекрыть любой шаблонный
target_include_directories(
  bootloader_fatfs
  PUBLIC include # ffconf.h, виден потребителям (sd_update.c)
  PRIVATE src)

target_link_libraries(
  bootloader_fatfs
  PUBLIC port_fatfs_sd # diskio_sd.h, ff.h, fsl_sd_disk.h, bsp_sd
  PRIVATE bsp_sdmmc_config) # SD_ENABLED транзитивно из bsp_sd, но явно для
                            # ясности

target_compile_options(bootloader_fatfs PRIVATE -w)
