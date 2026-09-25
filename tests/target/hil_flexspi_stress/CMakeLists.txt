set(TARGET_NAME test_hil_flexspi_stress)

# RAM-прошивка: код в ITCM, FlexSPI перенастраивается на лету (PLAN.md, фаза 0.2).
add_executable(${TARGET_NAME} main.c qspi_check.c ${BSP_GENERATED}/clock_config.c ${BSP_STARTUP_FILE}
                              ${BSP_SYSCALLS_FILE})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_options(
  ${TARGET_NAME}
  PRIVATE
  -T${CMAKE_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_ram.ld
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.map)

target_link_libraries(${TARGET_NAME} PRIVATE bsp_boot_ram bsp_board bsp_led bsp_tick bsp_uart_host
                                             sdk_flexspi)

add_custom_command(
  TARGET ${TARGET_NAME}
  POST_BUILD
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
  COMMENT "Size: ${TARGET_NAME}")
