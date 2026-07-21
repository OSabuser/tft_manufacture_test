add_library(tft_app_ui_fallback STATIC src/fallback.c)

target_include_directories(tft_app_ui_fallback PUBLIC include/)

target_link_libraries(
  tft_app_ui_fallback
  PUBLIC tft_app_elevator_model tft_app_controller
  PRIVATE tft_app_gfx)
