add_library(tft_app_sul_uim STATIC src/uim.c)

target_include_directories(tft_app_sul_uim PUBLIC include/)

target_link_libraries(tft_app_sul_uim PUBLIC tft_app_elevator_model
                                             tft_app_sul_headers)
