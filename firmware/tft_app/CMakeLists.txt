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
# Версия образа — ЕДИНСТВЕННЫЙ источник правды (см. docs/RELEASE_PROCESS.md).
#
# Отсюда её забирает imgtool при подписи (через app_version.txt ниже), и она
# попадает в заголовок MCUboot. Приложение читает версию ОТТУДА, в рантайме, —
# отдельного version.h нет намеренно: два места разъезжаются, заголовок образа
# разъехаться не может.
#
# ⚠️ Версия не косметическая: загрузчик по ней выбирает слот и решает,
# принимать ли обновление с SD (image_version_compare() в update_policy.c
# сравнивает major.minor.revision; build_num НЕ участвует). Образ с той же
# версией, что установленный, будет ПРОПУЩЕН — обновление молча не применится.
# -----------------------------------------------------------------------------
project(
  tft_app
  VERSION 0.1.0
  LANGUAGES C ASM)

# Версия в файл — рецепт подписи (just/build.just) читает её отсюда, чтобы не
# держать вторую копию числа в другом языке.
file(WRITE "${CMAKE_BINARY_DIR}/app_version.txt" "${tft_app_VERSION}")

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
# Доменные/сервисные/презентационные слои — каждый своя статическая либа
# (ARCH.md §3, конвенция bsp/<module>). tft_app_* префикс — коллизии имён.
# -----------------------------------------------------------------------------
add_subdirectory(src/domain)
add_subdirectory(src/services)
add_subdirectory(src/ui)
add_subdirectory(src/menu)

# -----------------------------------------------------------------------------
# app — исполняемый образ слота
# -----------------------------------------------------------------------------
include(${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/bootutil_sources.cmake)

add_executable(
  app
  src/app/main.c          # main() + wiring + FreeRTOS-хуки
  src/app/task_bringup.c  # одноразовый bring-up (QSPI/settings/self-confirm/SDRAM+gfx+CAN) → создаёт остальные задачи
  src/app/task_sul_rx.c   # приём CAN → decode → controller (WDOG/heartbeat безусловно, CAN-работа под !g_menu_active)
  src/app/task_menu.c     # модель меню: кнопки/hold-to-enter/nav/edit/save — НЕ рисует
  src/app/task_render.c   # презентация: единственный вызывающий gfx_present(), event-driven
  src/app/dispatcher.c    # диспетчерский вход opto IN1/IN2 (§3.4) — не задача, обвязка bsp_opto
  src/app/crash_log.c     # причина предыдущего сброса (.noinit-запись) — диагностика зависаний
  src/app/heartbeat.c     # супервизор живости задач — основание кормить WDOG (§3.7)
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
  # log_mutex.c — FreeRTOS strong-override весов log_mutex_{init,lock,unlock}
  # (weak NOP в utils/log/log.c). Компилируется ПРЯМО в app (не отдельной
  # либой) — как tick.c: FreeRTOS-специфика только этой цели, нужна во ВСЕХ
  # сборках tft_app (без неё — гонка на общем static-буфере логгера между
  # bringup_task/sul_rx_task/menu_task/render_task, см. PLAN.md).
  ${CMAKE_SOURCE_DIR}/port/log/src/log_mutex.c
  ${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/flash_map_backend.c
  ${MCUBOOT_BOOTUTIL_DIR}/src/bootutil_public.c)

target_include_directories(
  app PRIVATE src/app ${MCUBOOT_BOOTUTIL_INCLUDES}
              ${CMAKE_SOURCE_DIR}/firmware/bootloader/src) # led_status.h

# __STARTUP_INITIALIZE_RAMFUNCTION — startup копирует .ram_function из flash в
# ITCM. БЕЗ него ramfunc-код bsp_qspi (AT_QUICKACCESS_SECTION_CODE) в ITCM не
# попадает → запись QSPI виснет (см. bsp/qspi_flash/README.md).
# __STARTUP_INITIALIZE_NONCACHEDATA — аналогично для .ncache/.ncache.init:
# framebuffer (services/gfx, AT_NONCACHEABLE_SECTION_ALIGN) размещён в SDRAM
# non-cacheable регионе (cmake/linker/..._app_slot.ld, m_sdram_ncache) — по
# рекомендации NXP для буферов, которые ELCDIF читает по DMA (см.
# bsp/sdram/README.md и OLD_PROJECT/source/display/image_cache.c).
target_compile_definitions(
  app PRIVATE APP_OWN_SLOT_ID=0 __STARTUP_CLEAR_BSS __STARTUP_INITIALIZE_RAMFUNCTION
              __STARTUP_INITIALIZE_NONCACHEDATA
              # LOG_LEVEL=4 (DEBUG) — компайл-тайм ПОЛ для рантайм-уровня (§3.9).
              # Без него Release собирался бы с LOG_LEVEL=OFF (фолбэк log.h по
              # NDEBUG): ВСЕ LOG_* вырезаются препроцессором, и пункт меню «Логи»
              # гейтит пустоту. Цена — строковые литералы и вызовы log_write()
              # остаются в образе.
              #
              # Именно PRIVATE на таргете `app`, а НЕ глобальная cache-переменная
              # LOG_LEVEL: та ушла бы в `utils` как PUBLIC и изменила бы Release
              # ВЫПУЩЕННОГО загрузчика 1.0.0. Все LOG_* в tft_app живут в
              # src/app/*.c, которые компилируются прямо в этот таргет, — до них
              # PRIVATE-дефайн доходит.
              LOG_LEVEL=4)

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
          bsp_button
          bsp_opto # диспетчерский вход IN1/IN2 (§3.4), app/dispatcher.c
          bsp_wdog
          bsp_boot_state
          bsp_qspi_flash
          bsp_uart_host
          bsp_sdram
          bsp_boot_xip_no_dcd
          freertos_kernel
          port_log_uart # UART диагностика (utils_log + bsp_uart_host, LPUART1/MCU-Link VCOM)
          # Фаза 1: walking skeleton (CAN → декодер → контроллер → fallback-рендер)
          tft_app_sul
          tft_app_sul_transport_can
          # Фаза 3.3: демо-протокол (транспорт — синтетический, без bsp)
          tft_app_sul_transport_demo
          tft_app_controller
          tft_app_gfx
          tft_app_ui_fallback
          # Фаза 3.1: персист ядра настроек (QSPI 0x450000)
          tft_app_settings_store
          # Фаза 3.2.1/3.2.2: движок меню (модель) + рендер
          tft_app_menu
          tft_app_ui_menu)

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
