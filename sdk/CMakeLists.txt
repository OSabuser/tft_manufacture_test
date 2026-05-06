# sdk/CMakeLists.txt Драйверы NXP MCUXpresso SDK для MIMXRT1052CVJ5B Vendored:
# стабильная версия, не обновляется
#
# Добавление нового драйвера: см. docs/SDK_DRIVERS.md

if(BUILD_TESTS_HOST)
  return()
endif()

# -----------------------------------------------------------------------------
# Макрос для создания SDK драйвер-таргетов Использование: add_sdk_driver(ИМЯ
# ФАЙЛ_ИСХОДНИКА) Создаёт таргет sdk_ИМЯ с автоматическим подключением
# sdk_device
# -----------------------------------------------------------------------------
macro(add_sdk_driver DRIVER_NAME DRIVER_SOURCE)
  add_library(sdk_${DRIVER_NAME} STATIC
              devices/MIMXRT1052/drivers/${DRIVER_SOURCE})
  target_include_directories(sdk_${DRIVER_NAME}
                             PUBLIC devices/MIMXRT1052/drivers)
  target_link_libraries(sdk_${DRIVER_NAME} PUBLIC sdk_device)
endmacro()

# -----------------------------------------------------------------------------
# Базовый таргет — нужен всегда, подключается транзитивно через bsp_board
# -----------------------------------------------------------------------------
add_library(sdk_device STATIC devices/MIMXRT1052/system_MIMXRT1052.c)
target_include_directories(
  sdk_device PUBLIC devices/MIMXRT1052 devices/MIMXRT1052/drivers
                    devices/MIMXRT1052/periph CMSIS/Core/Include/)

target_compile_definitions(sdk_device PUBLIC CPU_MIMXRT1052CVJ5B)

# -----------------------------------------------------------------------------
# Утилиты — базовые вещи, нужны большинству драйверов
# -----------------------------------------------------------------------------
add_library(sdk_common STATIC devices/MIMXRT1052/drivers/fsl_common.c
                              devices/MIMXRT1052/drivers/fsl_common_arm.c)
target_include_directories(sdk_common PUBLIC devices/MIMXRT1052/drivers)
target_compile_definitions(
  sdk_common PUBLIC SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY=600000000U
                    SDK_DELAY_USE_DWT)
target_link_libraries(sdk_common PUBLIC sdk_device)
add_sdk_driver(clock fsl_clock.c)

# clock зависит от common
target_link_libraries(sdk_clock PUBLIC sdk_common)

# -----------------------------------------------------------------------------
# Драйверы периферии
# -----------------------------------------------------------------------------
add_sdk_driver(gpio fsl_gpio.c)
add_sdk_driver(lpuart fsl_lpuart.c)
add_sdk_driver(flexcan fsl_flexcan.c)
add_sdk_driver(usdhc fsl_usdhc.c)
add_sdk_driver(lpi2c fsl_lpi2c.c)
add_sdk_driver(pwm fsl_pwm.c)
add_sdk_driver(adc fsl_adc.c)
add_sdk_driver(elcdif fsl_elcdif.c)
add_sdk_driver(cache fsl_cache.c)

# Драйверы которые зависят от clock
target_link_libraries(sdk_lpuart PUBLIC sdk_clock)
target_link_libraries(sdk_flexcan PUBLIC sdk_clock)
target_link_libraries(sdk_usdhc PUBLIC sdk_clock)
target_link_libraries(sdk_lpi2c PUBLIC sdk_clock)
target_link_libraries(sdk_pwm PUBLIC sdk_clock)
target_link_libraries(sdk_adc PUBLIC sdk_clock)
target_link_libraries(sdk_elcdif PUBLIC sdk_clock)

# -----------------------------------------------------------------------------
# SEMC и FlexSPI — инициализируются через DCD и FDCB, драйверы нужны только если
# обращаешься к ним явно из кода
# -----------------------------------------------------------------------------
add_sdk_driver(semc fsl_semc.c)
add_sdk_driver(flexspi fsl_flexspi.c)

target_link_libraries(sdk_semc PUBLIC sdk_clock)
target_link_libraries(sdk_flexspi PUBLIC sdk_clock)

# -----------------------------------------------------------------------------
# USB middleware — ручные таргеты (middleware/usb/, не drivers/)
# -----------------------------------------------------------------------------

# --- sdk_osa_bm: OS Abstraction Layer (bare-metal) ---
# Объявлен первым — от него зависят USB таргеты через fsl_os_abstraction.h
add_library(sdk_osa_bm STATIC components/osa/fsl_os_abstraction_bm.c
                              components/lists/fsl_component_generic_list.c)
target_include_directories(sdk_osa_bm PUBLIC components/osa components/lists)
target_link_libraries(sdk_osa_bm PUBLIC sdk_device sdk_common sdk_usb_config)
target_compile_options(sdk_osa_bm PRIVATE -w)

# --- sdk_usb_common: заголовки USB стека ---
add_library(sdk_usb_common INTERFACE)
target_include_directories(sdk_usb_common SYSTEM
                           INTERFACE middleware/usb/include)

# --- sdk_usb_config: application-specific конфиги ---
add_library(sdk_usb_config INTERFACE)
target_include_directories(sdk_usb_config SYSTEM
                           INTERFACE ${CMAKE_SOURCE_DIR}/bsp/usb_cdc/src)

# --- sdk_usb_device_ehci: EHCI контроллер + DCI абстракция ---
add_library(sdk_usb_device_ehci STATIC middleware/usb/device/usb_device_dci.c
                                       middleware/usb/device/usb_device_ehci.c)
target_include_directories(sdk_usb_device_ehci PUBLIC middleware/usb/device)
target_link_libraries(
  sdk_usb_device_ehci PUBLIC sdk_device sdk_common sdk_usb_common
                             sdk_usb_config sdk_osa_bm)
target_compile_options(sdk_usb_device_ehci PRIVATE -w)

# --- sdk_usb_phy: USB PHY ---
add_library(sdk_usb_phy STATIC middleware/usb/phy/usb_phy.c)
target_include_directories(sdk_usb_phy PUBLIC middleware/usb/phy)
target_link_libraries(sdk_usb_phy PUBLIC sdk_device sdk_common sdk_usb_common
                                         sdk_usb_config sdk_osa_bm)
target_compile_options(sdk_usb_phy PRIVATE -w)

# =============================================================================
# SDMMC middleware — SD-карта через USDHC, non-blocking (interrupt-driven).
#
# НЕ зависит от ffconf.h → безопасно компилировать один раз как общую
# статическую библиотеку. Оба бинарника (firmware_test, tft_app) линкуются к
# одному и тому же sdk_sdmmc_sd.
#
# =============================================================================
add_library(
  sdk_sdmmc_sd STATIC
  middleware/sdmmc/sd/fsl_sd.c middleware/sdmmc/common/fsl_sdmmc_common.c
  middleware/sdmmc/osa/fsl_sdmmc_osa.c
  middleware/sdmmc/host/usdhc/non_blocking/fsl_sdmmc_host.c)

target_include_directories(
  sdk_sdmmc_sd
  PUBLIC middleware/sdmmc/sd middleware/sdmmc/common middleware/sdmmc/osa
         middleware/sdmmc/host/usdhc/non_blocking middleware/sdmmc/host/usdhc)

target_link_libraries(sdk_sdmmc_sd PUBLIC sdk_usdhc sdk_osa_bm sdk_cache
                                          sdk_common)

# Подавляем предупреждения SDK — они нам не принадлежат.
target_compile_options(sdk_sdmmc_sd PRIVATE -w)

# =============================================================================
# FatFS — только заголовки и переменные с путями к исходникам.
#
# ff.c и fsl_sd_disk.c НЕ компилируются здесь, потому что оба включают ff.h →
# ffconf.h, который разный у firmware_test (bare-metal) и tft_app (FreeRTOS).
# Каждый бинарник сам добавляет их в свои sources.
#
# Использование в CMakeLists.txt бинарника:
# target_link_libraries(my_fatfs_target PRIVATE sdk_fatfs_headers)
# target_sources(my_fatfs_target PRIVATE ${SDK_FATFS_FF_SRC}
# ${SDK_FATFS_SD_DISK_SRC})
# =============================================================================
add_library(sdk_fatfs_headers INTERFACE)
target_include_directories(
  sdk_fatfs_headers INTERFACE middleware/fatfs/source
                              middleware/fatfs/source/fsl_sd_disk)
# Транзитивно тащим SDMMC: fsl_sd_disk.c вызывает SD_*() из sdk_sdmmc_sd.
target_link_libraries(sdk_fatfs_headers INTERFACE sdk_sdmmc_sd)

# Пути к исходникам, которые должны компилироваться per-binary. CACHE INTERNAL —
# видны во всём дереве, не переопределяются.
set(SDK_FATFS_FF_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/fatfs/source/ff.c
    CACHE INTERNAL
          "FatFS: путь к ff.c (компилировать per-binary с нужным ffconf.h)")

set(SDK_FATFS_SD_DISK_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/fatfs/source/fsl_sd_disk/fsl_sd_disk.c
    CACHE
      INTERNAL
      "FatFS: путь к fsl_sd_disk.c (компилировать per-binary с нужным ffconf.h)"
)
