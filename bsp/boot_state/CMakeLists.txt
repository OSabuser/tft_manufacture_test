if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_boot_state STATIC src/boot_state.c)

target_include_directories(
  bsp_boot_state
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_boot_state
  PRIVATE sdk_src)
