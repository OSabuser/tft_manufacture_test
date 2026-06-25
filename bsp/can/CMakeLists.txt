if(BUILD_TESTS_HOST)
  set(CAN_HOST_SOURCES mocks/can_mocks.c)
else()

  set(CAN_HOST_SOURCES src/can.c)
endif()

add_library(bsp_can STATIC ${CAN_HOST_SOURCES})

target_include_directories(
  bsp_can
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
         $<$<BOOL:${BUILD_TESTS_HOST}>:${CMAKE_CURRENT_SOURCE_DIR}/mocks>)

target_link_libraries(
  bsp_can
  PUBLIC bsp_status
  PRIVATE bsp_tick utils sdk_clock sdk_flexcan
          $<$<BOOL:${BUILD_TESTS_HOST}>:fff>)
