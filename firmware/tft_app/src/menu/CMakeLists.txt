# menu — движок меню настроек (§8). Модель (menu.c, чистая) + боевое дерево
# (menu_tree.c, данные). Рендер — отдельно в ui/menu.
add_library(tft_app_menu STATIC src/menu.c src/menu_tree.c)

target_include_directories(tft_app_menu PUBLIC include/)

# settings_store — только типы settings_t (заголовок чистый, без QSPI-кода).
target_link_libraries(tft_app_menu PUBLIC tft_app_settings_store)
