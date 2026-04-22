# bsp/qspi_flash/CMakeLists.txt BSP QSPI — W25Q64/128/256/512 через FlexSPI1.

if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_qspi_flash STATIC src/qspi_flash.c)

target_include_directories(
  bsp_qspi_flash
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_qspi_flash
  PUBLIC bsp_status
  PRIVATE bsp_board sdk_flexspi)
