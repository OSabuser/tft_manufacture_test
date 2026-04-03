if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_usb_cdc STATIC src/usb_cdc.c src/usb_cdc_descriptors.c
                               src/usb_cdc_hw.c src/usb_device_ch9.c)

target_include_directories(
  bsp_usb_cdc
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_usb_cdc
  PUBLIC bsp_status
  PRIVATE bsp_board bsp_tick sdk_usb_device_ehci sdk_usb_phy sdk_osa_bm)

target_compile_options(bsp_usb_cdc PRIVATE -w)
