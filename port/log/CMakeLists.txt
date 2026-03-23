# =============================================================================
# port_log_uart — UART-адаптер для utils_log.
#
# Связывает utils_log с bsp_uart_host: - регистрирует bsp_uart_host_write() как
# write callback - предоставляет strong-реализацию log_get_timestamp_ms() →
# bsp_tick_get_ms()
#
# Не компилируется для host-тестов (bsp_uart_host там недоступен).
# =============================================================================

if(BUILD_TESTS_HOST)
  return()
endif()

add_library(port_log_uart STATIC src/log_uart.c)

target_include_directories(port_log_uart PUBLIC include/)

target_link_libraries(
  port_log_uart PUBLIC utils # несёт log.h и LOG_LEVEL транзитивно
                       bsp_uart_host bsp_tick)
# Явный include на случай если utils обрабатывается после port в корневом CMake.
# Дублирует транзитивный путь из utils — безвредно.
target_include_directories(port_log_uart PUBLIC ${CMAKE_SOURCE_DIR}/utils)
