# tests/host/CMakeLists.txt

# -----------------------------------------------------------------------------
# Вспомогательная функция добавления host-теста.
#
# Использование: add_host_test( NAME       <имя теста> SOURCES    <тест.c>
# <тестируемый_модуль.c> ... MOCKS      <путь к папке с stub-хедерами>   #
# опционально )
#
# Каждый тест — отдельный исполняемый файл. CTest запускает их все.
# -----------------------------------------------------------------------------
function(add_host_test)
  cmake_parse_arguments(ARG "" "NAME" "SOURCES;MOCKS" ${ARGN})

  add_executable(${ARG_NAME} ${ARG_SOURCES})

  target_link_libraries(${ARG_NAME} PRIVATE lib_external)

  # stub-хедеры подключаются как PRIVATE — не торчат в других таргетах
  foreach(mock_dir IN LISTS ARG_MOCKS)
    target_include_directories(${ARG_NAME} PRIVATE ${mock_dir})
  endforeach()

  # публичные хедеры BSP нужны для #include "bsp/led.h"
  target_include_directories(${ARG_NAME}
                             PRIVATE ${PROJECT_SOURCE_DIR}/bsp/led/include)

  add_test(
    NAME ${ARG_NAME}
    COMMAND ${ARG_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endfunction()

# =============================================================================
# Host тесты
# =============================================================================

set(BSP_MOCKS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mocks)

# -----------------------------------------------------------------------------
# bsp_led
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_bsp_led
  SOURCES
  test_bsp_led.c
  ${PROJECT_SOURCE_DIR}/bsp/led/src/led.c # тестируемый модуль напрямую
  MOCKS
  ${BSP_MOCKS_DIR} # fsl_gpio.h, pin_mux.h, board.h
)
