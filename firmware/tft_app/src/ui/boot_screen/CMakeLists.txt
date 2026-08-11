add_library(tft_app_ui_boot_screen STATIC src/boot_screen_compose.c
                                          src/boot_screen_render.c)

target_include_directories(tft_app_ui_boot_screen PUBLIC include/)

target_link_libraries(tft_app_ui_boot_screen PRIVATE tft_app_gfx)
