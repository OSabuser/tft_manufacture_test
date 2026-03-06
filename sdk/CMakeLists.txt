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
