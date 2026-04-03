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
add_sdk_driver(common fsl_common.c)
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

# Драйверы которые зависят от clock
target_link_libraries(sdk_lpuart PUBLIC sdk_clock)
target_link_libraries(sdk_flexcan PUBLIC sdk_clock)
target_link_libraries(sdk_usdhc PUBLIC sdk_clock)
target_link_libraries(sdk_lpi2c PUBLIC sdk_clock)
target_link_libraries(sdk_pwm PUBLIC sdk_clock)
target_link_libraries(sdk_adc PUBLIC sdk_clock)

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

# --- sdk_usb_device_ch9: Chapter 9 standard requests ---
# add_library(sdk_usb_device_ch9 STATIC middleware/usb/device/usb_device_ch9.c)
# target_include_directories( sdk_usb_device_ch9 PUBLIC middleware/usb/device
# middleware/usb/device/class) target_link_libraries(sdk_usb_device_ch9 PUBLIC
# sdk_usb_device_ehci) target_compile_options(sdk_usb_device_ch9 PRIVATE -w)

# --- sdk_usb_phy: USB PHY ---
add_library(sdk_usb_phy STATIC middleware/usb/phy/usb_phy.c)
target_include_directories(sdk_usb_phy PUBLIC middleware/usb/phy)
target_link_libraries(sdk_usb_phy PUBLIC sdk_device sdk_common sdk_usb_common
                                         sdk_usb_config sdk_osa_bm)
target_compile_options(sdk_usb_phy PRIVATE -w)
