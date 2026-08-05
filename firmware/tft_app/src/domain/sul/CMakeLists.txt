# tft_app_sul_headers — INTERFACE, только domain/sul.h (типы sul_frame_t/
# sul_status_t/sul_driver_t). Разделено от реализации реестра (ниже), чтобы
# tft_app_sul_nku_can мог использовать типы без циклической зависимости: nku_can
# нужны только типы sul.h, а sul_registry.c сам зовёт nku_can_decode.
add_library(tft_app_sul_headers INTERFACE)
target_include_directories(tft_app_sul_headers INTERFACE include/)
target_link_libraries(tft_app_sul_headers INTERFACE tft_app_elevator_model)

add_subdirectory(nku_can)
add_subdirectory(uim)
add_subdirectory(demo)
add_subdirectory(transport/can)
add_subdirectory(transport/demo)

# tft_app_sul — реализация реестра. Явно знает обо всех драйверах (nku_can,
# demo); добавление протокола (Фаза 8) — новая запись в sul_registry.c + новая
# зависимость здесь. Транспорты (transport/*) сюда НЕ линкуются — держит реестр
# host-тестируемым без bsp; wiring транспорт<->протокол — app-слой
# (task_sul_rx.c).
add_library(tft_app_sul STATIC src/sul_registry.c)
target_link_libraries(
  tft_app_sul PUBLIC tft_app_sul_headers tft_app_sul_nku_can tft_app_sul_demo
                     tft_app_sul_uim)
