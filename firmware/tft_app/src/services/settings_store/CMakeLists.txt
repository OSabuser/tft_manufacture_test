# services/settings_store — персист ядра настроек (§8, §10).
# settings_codec.c — чистая сериализация/CRC/дефолты (host-тест);
# settings_store.c — тонкий flash-адаптер поверх bsp_qspi_flash.
add_library(
  tft_app_settings_store STATIC
  src/settings_store.c
  src/settings_codec.c)

target_include_directories(
  tft_app_settings_store
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  tft_app_settings_store
  PUBLIC bsp_qspi_flash   # bsp/qspi_flash.h (API + геометрия), bsp/status.h транзитивно
         tft_app_partition) # смещения регионов (§10)
