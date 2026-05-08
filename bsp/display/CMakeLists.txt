# bsp/display/CMakeLists.txt BSP ELCDIF RGB Display

if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_display STATIC src/display.c)

target_include_directories(
  bsp_display
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_display
  PUBLIC bsp_status
  PRIVATE bsp_board sdk_elcdif)
