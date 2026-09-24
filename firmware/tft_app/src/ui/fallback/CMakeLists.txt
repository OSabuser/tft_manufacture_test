# fallback_sprites.c — ЧИСТЫЙ выбор «состояние → id спрайта», host-тестируется
# (tests/host/tft_app_fallback_sprites), от gfx не зависит.
# fallback_sprite_table.c — привязка id → вкомпилированная картинка, зависит от
# gfx и от сгенерированных .c; проверяется на стенде.
#
# sprites/*.c — СГЕНЕРИРОВАНЫ tools/host/png2sprite.py из
# assets/fallback_sprites/*.png (руками не правятся, регенерируются командой
# `just host::pack-fallback-sprites`). Перечислены явно, а не через GLOB:
# добавление спрайта и так требует правки fallback_sprite_table.c, так что
# «забыть файл» не выйдет — линковка упадёт на неразрешённом символе.
set(FALLBACK_SPRITES
    sprites/arrow_up.c
    sprites/arrow_down.c
    sprites/arrow_double.c
    sprites/mode_lading.c
    sprites/mode_maintenance.c
    sprites/mode_seismic.c
    sprites/mode_overload.c
    sprites/mode_fire_alarm.c
    sprites/mode_fireman.c
    sprites/mode_evacuation.c
    sprites/mode_error.c
    sprites/dispatcher_calling.c
    sprites/dispatcher_talking.c)

add_library(tft_app_ui_fallback STATIC src/fallback.c src/fallback_sprites.c
                                       src/fallback_sprite_table.c ${FALLBACK_SPRITES})

target_include_directories(tft_app_ui_fallback PUBLIC include/)

target_link_libraries(
  tft_app_ui_fallback
  PUBLIC tft_app_elevator_model tft_app_controller
  PRIVATE tft_app_gfx)
