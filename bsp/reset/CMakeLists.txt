if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_reset STATIC src/reset.c src/reset_decode.c)

target_include_directories(
  bsp_reset
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(bsp_reset PRIVATE sdk_src)
