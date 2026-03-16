# tests/host/CMakeLists.txt

# -----------------------------------------------------------------------------
# Вспомогательная функция добавления host-теста.
#
# Использование: add_host_test( NAME    <имя теста> SOURCES <тест.c> [<модуль.c>
# ...]   # исходники теста + тестируемый код INCLUDES <dir> [<dir> ...] #
# дополнительные include-пути (опц.) MOCKS   <dir> [<dir> ...]           # папки
# со stub-хедерами (опц.) )
#
# Каждый тест — отдельный исполняемый файл. CTest запускает их все.
#
# Соглашения: - lib_external подключается всегда (Unity + fff). - INCLUDES и
# MOCKS — явные: каждый тест сам декларирует свои зависимости. Нет скрытых
# глобальных include-путей, специфичных для одного модуля.
# -----------------------------------------------------------------------------
function(add_host_test)
  cmake_parse_arguments(ARG "" "NAME" "SOURCES;INCLUDES;MOCKS" ${ARGN})

  add_executable(${ARG_NAME} ${ARG_SOURCES})

  target_link_libraries(${ARG_NAME} PRIVATE lib_external)

  # Явные include-пути, специфичные для теста
  foreach(inc_dir IN LISTS ARG_INCLUDES)
    target_include_directories(${ARG_NAME} PRIVATE ${inc_dir})
  endforeach()

  # Stub-хедеры для моков (PRIVATE — не торчат в других таргетах)
  foreach(mock_dir IN LISTS ARG_MOCKS)
    target_include_directories(${ARG_NAME} PRIVATE ${mock_dir})
  endforeach()

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
  led/test_bsp_led.c
  ${PROJECT_SOURCE_DIR}/bsp/led/src/led.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/bsp/led/include # для #include "bsp/led.h"
  MOCKS
  ${BSP_MOCKS_DIR} # fsl_gpio.h, pin_mux.h, board.h
)

# -----------------------------------------------------------------------------
# utils/ring_buffer
# -----------------------------------------------------------------------------
add_host_test(
  NAME
  test_ring_buffer
  SOURCES
  ring_buffer/test_ring_buffer.c
  ${PROJECT_SOURCE_DIR}/utils/ring_buffer/ring_buffer.c
  INCLUDES
  ${PROJECT_SOURCE_DIR}/utils # для #include "ring_buffer/ring_buffer.h"
)

# -----------------------------------------------------------------------------
# Тест таймаут-паттерна, используемого в bsp/tick
# -----------------------------------------------------------------------------
add_host_test(
  NAME test_timeout_pattern SOURCES timeout/test_timeout_pattern.c
  # нет INCLUDES и MOCKS — чистая математика, нет зависимостей
)
