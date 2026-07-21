add_library(tft_app_controller STATIC src/controller.c src/mode_priority.c)

target_include_directories(tft_app_controller PUBLIC include/)

target_link_libraries(tft_app_controller PUBLIC tft_app_elevator_model)
