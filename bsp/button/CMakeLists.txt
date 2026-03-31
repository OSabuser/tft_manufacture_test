if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_button STATIC src/button.c)

target_include_directories(
  bsp_button
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_button
  PUBLIC bsp_status
  PRIVATE bsp_board sdk_gpio)
