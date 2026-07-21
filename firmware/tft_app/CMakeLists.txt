# firmware/tft_app/CMakeLists.txt
#
# tft_app — прошивка лифтового индикатора. Собирается как MCUboot-слот-образ
# (Direct-XIP), подписывается imgtool (см. just build::sign-app). Архитектура —
# ARCH.md, план фаз — PLAN.md.
#
# Фаза 0 (каркас): FreeRTOS-мигалка + кормление WDOG + рантайм self-confirm
# слота + UART диагностика (LPUART1/MCU-Link VCOM — см. src/app/main.c про
# отказ от USB CDC под FreeRTOS для этой цели).

if(BUILD_TESTS_HOST)
  return()
endif()

# -----------------------------------------------------------------------------
# FreeRTOS ядро — первый RTOS-потребитель в репозитории. Собираем свой STATIC
# таргет из вендоренных исходников SDK (порт GCC/ARM_CM4F пригоден для cm7f,
# heap_4). Конфиг — src/app/FreeRTOSConfig.h.
# -----------------------------------------------------------------------------
set(FREERTOS_DIR ${CMAKE_SOURCE_DIR}/sdk/rtos/freertos/freertos-kernel)

add_library(
  freertos_kernel STATIC
  ${FREERTOS_DIR}/tasks.c
  ${FREERTOS_DIR}/list.c
  ${FREERTOS_DIR}/queue.c
  ${FREERTOS_DIR}/timers.c
  ${FREERTOS_DIR}/event_groups.c
  ${FREERTOS_DIR}/stream_buffer.c
  ${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
  ${FREERTOS_DIR}/portable/MemMang/heap_4.c)

target_include_directories(
  freertos_kernel
  PUBLIC ${FREERTOS_DIR}/include
         ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
         ${CMAKE_CURRENT_SOURCE_DIR}/src/app) # FreeRTOSConfig.h

# core_cm7.h, MIMXRT1052.h, SystemCoreClock
target_link_libraries(freertos_kernel PUBLIC sdk_device)

# Вендоренное ядро — не наш стиль/warnings.
target_compile_options(freertos_kernel PRIVATE -w)

# -----------------------------------------------------------------------------
# app — исполняемый образ слота
# -----------------------------------------------------------------------------
include(${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/bootutil_sources.cmake)

add_executable(
  app
  src/app/main.c
  ${BSP_GENERATED}/clock_config.c # BOARD_BootClockRUN (зовётся board_hw_init)
  ${BSP_STARTUP_FILE}
  ${BSP_SYSCALLS_FILE}
  # confirm_self(): flash_area_* + boot_set_next. Переиспользуем backend и
  # bootutil_public (общий с bootloader/test_stub). flash_map_backend.c зовёт
  # led_status_tick_install() — даём локальную no-op заглушку (boot_shims.c).
  src/app/boot_shims.c
  # bsp_tick, скомпилирован ПРЯМО в app (не через libbsp_tick.a) — режим задан
  # ниже (BSP_TICK_FREERTOS_MODE), чтобы не тянуть SysTick_Handler,
  # конфликтующий с FreeRTOS-портом. Приходит транзитивно через bsp_uart_host.
  ${CMAKE_SOURCE_DIR}/bsp/tick/src/tick.c
  ${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/flash_map_backend.c
  ${MCUBOOT_BOOTUTIL_DIR}/src/bootutil_public.c)

target_include_directories(
  app PRIVATE src/app ${MCUBOOT_BOOTUTIL_INCLUDES}
              ${CMAKE_SOURCE_DIR}/firmware/bootloader/src) # led_status.h

# __STARTUP_INITIALIZE_RAMFUNCTION — startup копирует .ram_function из flash в
# ITCM. БЕЗ него ramfunc-код bsp_qspi (AT_QUICKACCESS_SECTION_CODE) в ITCM не
# попадает → запись QSPI виснет (см. bsp/qspi_flash/README.md). Без USB CDC
# __STARTUP_INITIALIZE_NONCACHEDATA не нужен — ни один линкуемый модуль не
# кладёт данные в NonCacheable/NonCacheable.init (проверено).
target_compile_definitions(
  app PRIVATE APP_OWN_SLOT_ID=0 __STARTUP_CLEAR_BSS __STARTUP_INITIALIZE_RAMFUNCTION)

# Вендоренный bootutil_public.c — под своими warnings (как в bootloader/stub).
set_source_files_properties(
  ${MCUBOOT_BOOTUTIL_DIR}/src/bootutil_public.c
  PROPERTIES COMPILE_OPTIONS "${MCUBOOT_VENDORED_COMPILE_OPTIONS}")

# FreeRTOS-режим bsp_tick: без него tick.c определяет SysTick_Handler
# (bare-metal), конфликтующий с обработчиком FreeRTOS-порта. Компилируется прямо
# в app (link-line раньше архивов) → bare-metal member из libbsp_tick.a не
# извлекается.
set_source_files_properties(
  ${CMAKE_SOURCE_DIR}/bsp/tick/src/tick.c PROPERTIES COMPILE_DEFINITIONS
                                                      BSP_TICK_FREERTOS_MODE)

target_link_libraries(
  app
  PRIVATE bsp_board
          bsp_led
          bsp_wdog
          bsp_boot_state
          bsp_qspi_flash
          bsp_uart_host
          bsp_boot_xip_no_dcd
          freertos_kernel
          port_log_uart) # UART диагностика (utils_log + bsp_uart_host, LPUART1/MCU-Link VCOM)

target_link_options(
  app
  PRIVATE
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/app.map
  -Wl,--defsym=__slot_base__=0x60040000 # Slot A; Slot Б — на фазе обновления
  -Wl,--defsym=__stack_size__=0x1000
  -Wl,--defsym=__heap_size__=0x400
  -T${CMAKE_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_app_slot.ld)

set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_custom_command(
  TARGET app
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:app> ${CMAKE_BINARY_DIR}/app.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:app>
  COMMENT "Generating app.bin")
