# menu — движок меню настроек (§8). Фаза 3.2.1: чистая модель (без рендера/QSPI).
add_library(tft_app_menu STATIC src/menu.c)

target_include_directories(tft_app_menu PUBLIC include/)

# settings_store — только типы settings_t (заголовок чистый, без QSPI-кода).
target_link_libraries(tft_app_menu PUBLIC tft_app_settings_store)
