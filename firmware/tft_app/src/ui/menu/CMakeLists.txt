# ui/menu — рендер меню настроек (презентация, HIL). Фаза 3.2.2.
add_library(tft_app_ui_menu STATIC src/menu_view.c)

target_include_directories(tft_app_ui_menu PUBLIC include/)

target_link_libraries(
  tft_app_ui_menu
  PUBLIC tft_app_menu   # menu_ctx_t / menu_item_desc_t + запросы модели
  PRIVATE tft_app_gfx)  # примитивы + шрифты
