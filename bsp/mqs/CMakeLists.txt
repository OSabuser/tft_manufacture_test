# bsp/mqs/CMakeLists.txt

if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_mqs STATIC src/mqs.c src/mqs_amp.c)

target_include_directories(
  bsp_mqs
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_mqs
  PUBLIC bsp_status
  PUBLIC bsp_board
  PRIVATE sdk_sai_edma sdk_edma sdk_dmamux sdk_clock sdk_pwm sdk_xbara)
