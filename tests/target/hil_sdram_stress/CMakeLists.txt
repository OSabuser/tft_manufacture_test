set(TARGET_NAME test_hil_sdram_stress)

# -----------------------------------------------------------------------------
# DCD-варианты: dcd/<name>.txt → <build>/dcd/<name>.c (tools/host/dcd_tool.py,
# только stdlib Python) + таблица dcd_variants_table.c (генерируется здесь).
# -----------------------------------------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(DCD_TOOL ${CMAKE_SOURCE_DIR}/tools/host/dcd_tool.py)
set(DCD_OUT ${CMAKE_CURRENT_BINARY_DIR}/dcd)
file(GLOB DCD_VARIANT_TXT CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dcd/*.txt)
list(SORT DCD_VARIANT_TXT)

set(DCD_VARIANT_SOURCES)
set(DCD_TABLE_EXTERNS "")
set(DCD_TABLE_ROWS "")
foreach(txt IN LISTS DCD_VARIANT_TXT)
  get_filename_component(name ${txt} NAME_WE)
  set(out ${DCD_OUT}/${name}.c)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${Python3_EXECUTABLE} ${DCD_TOOL} ${txt} --c ${out} --symbol g_dcd_${name} --werror
    DEPENDS ${txt} ${DCD_TOOL}
    COMMENT "DCD variant: ${name}")
  list(APPEND DCD_VARIANT_SOURCES ${out})
  string(APPEND DCD_TABLE_EXTERNS
         "extern const uint8_t g_dcd_${name}[];\nextern const size_t g_dcd_${name}_size;\n")
  string(APPEND DCD_TABLE_ROWS "    {\"${name}\", g_dcd_${name}, &g_dcd_${name}_size},\n")
endforeach()

set(DCD_TABLE ${DCD_OUT}/dcd_variants_table.c)
file(
  WRITE ${DCD_TABLE}.in
  "/* Сгенерировано CMake (tests/target/hil_sdram_stress). Не редактировать. */\n"
  "#include \"dcd_variants.h\"\n\n${DCD_TABLE_EXTERNS}\n"
  "const dcd_variant_t g_dcd_variants[] = {\n${DCD_TABLE_ROWS}};\n\n"
  "const size_t g_dcd_variant_count = sizeof(g_dcd_variants) / sizeof(g_dcd_variants[0]);\n")
configure_file(${DCD_TABLE}.in ${DCD_TABLE} COPYONLY)

add_executable(
  ${TARGET_NAME}
  main.c mem_tests.c ${DCD_VARIANT_SOURCES} ${DCD_TABLE} ${BSP_GENERATED}/clock_config.c
  ${BSP_STARTUP_FILE} ${BSP_SYSCALLS_FILE})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_options(
  ${TARGET_NAME}
  PRIVATE
  -T${CMAKE_SOURCE_DIR}/cmake/linker/MIMXRT1052xxxxx_ram.ld
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.map)

target_link_libraries(
  ${TARGET_NAME} PRIVATE bsp_boot_ram bsp_board bsp_led bsp_tick bsp_uart_host
                         bsp_sdram)

add_custom_command(
  TARGET ${TARGET_NAME}
  POST_BUILD
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
  COMMENT "Size: ${TARGET_NAME}")
