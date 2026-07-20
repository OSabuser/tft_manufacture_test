# firmware/bootloader/test_stub/CMakeLists.txt
#
# Заглушка tft_app для аппаратной верификации корректной работы загрузчика
#
# Один main.c, много таргетов: адрес слота (--defsym __slot_base__) + частота
# мигания (STUB_BLINK_MS) — чтобы на глаз отличить, какой слот выбрал bootloader
# плюс три независимые оси confirm/hang/watchdog  — чтобы прогнать классы
# отказов A/B на реальном железе. Подписывается вручную imgtool.
#
include(
  ${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/bootutil_sources.cmake)

function(add_mcuboot_stub)
  cmake_parse_arguments(
    ARG ""
    "NAME;SLOT_BASE;BLINK_MS;OWN_SLOT_ID;CONFIRM_MODE;HANG_MODE;FEED_WDOG" ""
    ${ARGN})

  if(NOT DEFINED ARG_CONFIRM_MODE)
    set(ARG_CONFIRM_MODE 0) # 0 сразу / 1 отложенно / 2 никогда — см. main.c
  endif()
  if(NOT DEFINED ARG_HANG_MODE)
    set(ARG_HANG_MODE 0) # 0 никогда / 1 до health-mark / 2 после health-mark /
                         # 3 после confirm
  endif()
  if(NOT DEFINED ARG_FEED_WDOG)
    set(ARG_FEED_WDOG 1) # 1 кормить (дефолт, как в проде) / 0 не кормить
  endif()

  add_executable(
    ${ARG_NAME}
    main.c
    ${BSP_GENERATED}/clock_config.c
    ${BSP_STARTUP_FILE}
    ${BSP_SYSCALLS_FILE}
    ${CMAKE_SOURCE_DIR}/firmware/bootloader/mcuboot_port/flash_map_backend.c
    ${CMAKE_SOURCE_DIR}/firmware/bootloader/src/led_status.c
    ${MCUBOOT_BOOTUTIL_DIR}/src/bootutil_public.c)

  # led_status.h — flash_map_backend.c (общий с bootloader) зовёт
  # led_status_tick_install() из поблочного erase (см. led_status.h) — не
  # добавляет стабу нового поведения (окно установки здесь никогда не
  # открыто, tick — no-op), просто нужен для компиляции общего файла.
  target_include_directories(
    ${ARG_NAME} PRIVATE ${MCUBOOT_BOOTUTIL_INCLUDES}
                        ${CMAKE_SOURCE_DIR}/firmware/bootloader/src)

  target_compile_definitions(
    ${ARG_NAME}
    PRIVATE STUB_BLINK_MS=${ARG_BLINK_MS}
            STUB_OWN_SLOT_ID=${ARG_OWN_SLOT_ID}
            STUB_CONFIRM_MODE=${ARG_CONFIRM_MODE}
            STUB_HANG_MODE=${ARG_HANG_MODE}
            STUB_FEED_WDOG=${ARG_FEED_WDOG}
            __STARTUP_CLEAR_BSS)

  # Вендоренный bootutil_public.c — не наш стиль/warnings, тот же обход, что и
  # для остального bootutil (см. bootutil_sources.cmake). Наш код (main.c,
  # flash_map_backend.c) остаётся под обычными warnings.
  set_source_files_properties(
    ${MCUBOOT_BOOTUTIL_DIR}/src/bootutil_public.c
    PROPERTIES COMPILE_OPTIONS "${MCUBOOT_VENDORED_COMPILE_OPTIONS}")

  # bsp_wdog: загрузчик взводит WDOG перед прыжком, WDE — write-once, поэтому
  # образ ОБЯЗАН его кормить, если STUB_FEED_WDOG=1 (иначе reset-loop — либо
  # намеренно, для теста самого механизма). bsp_boot_state —
  # bsp_boot_health_mark(). bsp_qspi_flash — нужен flash_map_backend.c для
  # confirm_self().
  target_link_libraries(
    ${ARG_NAME}
    PRIVATE bsp_board
            bsp_led
            bsp_tick
            bsp_boot_xip_no_dcd
            bsp_wdog
            bsp_boot_state
            bsp_qspi_flash)

  target_link_options(
    ${ARG_NAME}
    PRIVATE
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_BINARY_DIR}/${ARG_NAME}.map
    -Wl,--defsym=__slot_base__=${ARG_SLOT_BASE}
    -Wl,--defsym=__stack_size__=0x400
    -Wl,--defsym=__heap_size__=0x400
    -T${CMAKE_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_mcuboot_slot.ld)

  set_target_properties(${ARG_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                               ${CMAKE_BINARY_DIR})

  add_custom_command(
    TARGET ${ARG_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${ARG_NAME}>
            ${CMAKE_BINARY_DIR}/${ARG_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${ARG_NAME}>
    COMMENT "Generating ${ARG_NAME}.bin")
endfunction()

# ─────────────────────────────────────────────────────────────────────────
# Базовые "здоровые" заглушки: подтверждаются сразу, никогда не виснут. Slot A
# мигает раз в 500 мс ("версия 1"), Slot Б — раз в 250 мс ("версия 2").
# CONFIRM_MODE/HANG_MODE/FEED_WDOG — дефолты (0/0/1).
# ─────────────────────────────────────────────────────────────────────────
add_mcuboot_stub(
  NAME
  test_slot_stub_a
  SLOT_BASE
  0x60040000
  BLINK_MS
  500
  OWN_SLOT_ID
  0)
add_mcuboot_stub(
  NAME
  test_slot_stub_b
  SLOT_BASE
  0x60240000
  BLINK_MS
  250
  OWN_SLOT_ID
  1)

# Класс A: никогда не подтверждается, виснет СРАЗУ (до health-mark) — свежий
# неподтверждённый образ виснет до всякого прогресса. WDOG-сброс → штатный
# MCUboot revert (copy_done без image_ok) → откат, без участия recovery.c.
add_mcuboot_stub(
  NAME
  test_slot_stub_a_hang
  SLOT_BASE
  0x60040000
  BLINK_MS
  500
  OWN_SLOT_ID
  0
  CONFIRM_MODE
  2
  HANG_MODE
  1)
add_mcuboot_stub(
  NAME
  test_slot_stub_b_hang
  SLOT_BASE
  0x60240000
  BLINK_MS
  250
  OWN_SLOT_ID
  1
  CONFIRM_MODE
  2
  HANG_MODE
  1)

# Класс B: подтверждается СРАЗУ (успевает помигать наблюдаемо — видно, что образ
# живой и подтверждён), виснет через STUB_CONFIRM_DELAY_MS + STUB_HANG_DELAY_MS
# (~6 c) после старта — уже ПОСЛЕ confirm. Штатный revert MCUboot тут не
# сработает (image_ok уже SET) — ловит именно recovery.c (счётчик
# watchdog-сбросов + фолбэк/recovery).
add_mcuboot_stub(
  NAME
  test_slot_stub_a_confirm_hang
  SLOT_BASE
  0x60040000
  BLINK_MS
  500
  OWN_SLOT_ID
  0
  CONFIRM_MODE
  0
  HANG_MODE
  3)
add_mcuboot_stub(
  NAME
  test_slot_stub_b_confirm_hang
  SLOT_BASE
  0x60240000
  BLINK_MS
  250
  OWN_SLOT_ID
  1
  CONFIRM_MODE
  0
  HANG_MODE
  3)
