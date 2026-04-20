# bsp/sdram/CMakeLists.txt bsp_sdram — верификация и тест внешней SDRAM
# (MT48LC16M16A2, 32 MB)
#
# Архитектурное решение [DECISION]: SEMC инициализируется DCD до main().
# bsp_sdram_init() только верифицирует доступность памяти — не трогает SEMC
# регистры.
#
# Зависимости: sdk_semc — fsl_semc.h нужен для SEMC_GetStatusFlags() в
# верификации bsp_tick — bsp_tick_get_ms() для замера времени тест-паттернов

if(BUILD_TESTS_HOST)
  return()
endif()

add_library(bsp_sdram STATIC src/sdram.c)

target_include_directories(
  bsp_sdram
  PUBLIC include/
  PRIVATE src/)

target_link_libraries(
  bsp_sdram
  PUBLIC bsp_status
  PRIVATE bsp_board bsp_tick sdk_semc)
