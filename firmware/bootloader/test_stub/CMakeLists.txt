# firmware/bootloader/test_stub/CMakeLists.txt
#
# Заглушка tft_app для аппаратной верификации Фазы 2 bootutil — см.
# firmware/bootloader/PLAN.md, "Аппаратная верификация Фазы 2". Не часть
# продукта — удалить, когда появится реальный tft_app.
#
# Два таргета из одного main.c: разный адрес слота (--defsym __slot_base__) и
# разная частота мигания (STUB_BLINK_MS) — чтобы на глаз отличить, какой слот
# выбрал bootloader. Постройка .bin, дальше подписывается вручную imgtool'ом
# (см. PLAN.md) — .bin сюда не должен попасть без подписи.

function(add_mcuboot_stub NAME SLOT_BASE BLINK_MS)
  add_executable(${NAME} main.c ${BSP_GENERATED}/clock_config.c
                         ${BSP_STARTUP_FILE} ${BSP_SYSCALLS_FILE})

  target_compile_definitions(${NAME} PRIVATE STUB_BLINK_MS=${BLINK_MS}
                                             __STARTUP_CLEAR_BSS)

  # bsp_wdog: загрузчик взводит WDOG перед прыжком, WDE — write-once, поэтому
  # образ ОБЯЗАН его кормить (иначе reset-loop). Заглушка — стенд-ин tft_app,
  # тоже кормит. См. bsp/wdog/README.md.
  target_link_libraries(${NAME} PRIVATE bsp_board bsp_led bsp_tick
                                        bsp_boot_xip_no_dcd bsp_wdog)

  target_link_options(
    ${NAME}
    PRIVATE
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_BINARY_DIR}/${NAME}.map
    -Wl,--defsym=__slot_base__=${SLOT_BASE}
    -Wl,--defsym=__stack_size__=0x400
    -Wl,--defsym=__heap_size__=0x400
    -T${CMAKE_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_mcuboot_slot.ld)

  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                           ${CMAKE_BINARY_DIR})

  add_custom_command(
    TARGET ${NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${NAME}>
            ${CMAKE_BINARY_DIR}/${NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${NAME}>
    COMMENT "Generating ${NAME}.bin")
endfunction()

# Slot A: 0x60040000, мигает раз в 500 мс ("версия 1")
add_mcuboot_stub(test_slot_stub_a 0x60040000 500)

# Slot Б: 0x60240000, мигает раз в 250 мс ("версия 2")
add_mcuboot_stub(test_slot_stub_b 0x60240000 250)
