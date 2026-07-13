if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_wdog STATIC src/wdog.c)

target_include_directories(
  bsp_wdog
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_wdog
  PUBLIC bsp_status
  PRIVATE sdk_wdog)
