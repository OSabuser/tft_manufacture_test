add_library(bsp_opto STATIC src/opto.c)

target_include_directories(bsp_opto PUBLIC include)

target_link_libraries(
  bsp_opto
  PUBLIC bsp_status
  PRIVATE bsp_tick bsp_board sdk_gpio)
