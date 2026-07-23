add_library(tft_app_sul_demo STATIC src/demo.c src/demo_route.c)

target_include_directories(tft_app_sul_demo PUBLIC include/)

target_link_libraries(tft_app_sul_demo PUBLIC tft_app_elevator_model tft_app_sul_headers)
