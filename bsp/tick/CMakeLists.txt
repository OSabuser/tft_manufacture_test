if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_tick STATIC src/tick.c)

target_include_directories(
  bsp_tick
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_tick PUBLIC bsp_board # board.h, pin_mux.h — транзитивно
)
