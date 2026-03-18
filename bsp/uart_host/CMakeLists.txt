if(BUILD_TESTS_HOST)
  set(UART_HOST_SOURCES mocks/uart_host_mock.c)
else()

  set(UART_HOST_SOURCES src/uart_host.c)
endif()

add_library(bsp_uart_host STATIC ${UART_HOST_SOURCES})

target_include_directories(
  bsp_uart_host
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
         $<$<BOOL:${BUILD_TESTS_HOST}>:${CMAKE_CURRENT_SOURCE_DIR}/mocks>)

target_link_libraries(
  bsp_uart_host
  PUBLIC bsp_status
  PRIVATE bsp_tick utils sdk_lpuart $<$<BOOL:${BUILD_TESTS_HOST}>:fff>)
