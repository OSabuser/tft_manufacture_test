if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_led STATIC src/led.c)

target_include_directories(
  bsp_led
  PUBLIC include/ # led.h доступен снаружи
  PRIVATE src/ # ничего приватного сейчас, но структура готова
)

target_link_libraries(
  bsp_led
  PUBLIC bsp_board # board.h, pin_mux.h — транзитивно
  PRIVATE sdk_gpio # fsl_gpio.h — не торчит наружу
)
