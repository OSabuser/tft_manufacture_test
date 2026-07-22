# services/gfx — Фаза 1 минимальный gfx: framebuffer (SDRAM non-cacheable) +
# RLE-шрифты (lcd-image-converter) + примитивы. См. include/services/gfx.h.

add_library(
  tft_app_gfx STATIC
  src/gfx.c
  fonts/FloorFontFallback.c
  fonts/SystemFont.c
  fonts/JBMono12.c)

target_include_directories(tft_app_gfx PUBLIC include/)

# Сгенерированные lcd-image-converter файлы ожидают tImage/tChar/tFont из
# bare-name "fonts.h": SystemFont.c явным #include <fonts.h> (резолвится
# через -I ниже), FloorFontFallback.c вообще без include-строки (более новая
# версия конвертера, полагается на внешний force-include). Один и тот же
# заголовок (fonts/include/fonts.h) закрывает оба случая. НЕ редактируем
# сгенерированные файлы напрямую — конвертер их перегенерирует при
# обновлении шрифта, ручная правка потеряется.
target_include_directories(tft_app_gfx PRIVATE fonts/include)
set_source_files_properties(
  fonts/FloorFontFallback.c fonts/SystemFont.c fonts/JBMono12.c
  PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/fonts/include/fonts.h;-w")

# sdk_common — fsl_common.h (AT_NONCACHEABLE_SECTION_ALIGN); sdk_pxp — PXP-компоновщик;
# freertos_kernel — семафор FRAME_DONE в gfx_present/ISR-колбэке (Фаза 3.2.4).
target_link_libraries(tft_app_gfx PUBLIC bsp_display bsp_sdram sdk_common PRIVATE sdk_pxp
                                          freertos_kernel)
