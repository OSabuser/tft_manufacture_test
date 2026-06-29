# bsp/provisioning/CMakeLists.txt

if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_provisioning STATIC src/provisioning.c)

target_include_directories(
  bsp_provisioning
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_provisioning
  PUBLIC bsp_status
  PRIVATE bsp_board)
