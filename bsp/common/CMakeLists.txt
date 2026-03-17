# =============================================================================
# bsp_status — общий тип возврата для BSP-модулей
#
# INTERFACE-библиотека: нет .c файлов, только пробрасывает include-путь.
# Работает на любой платформе (ARM target и host unit-тесты) без ifdef.
#
# Использование: target_link_libraries(<bsp_module> PUBLIC bsp_status)
# =============================================================================

cmake_minimum_required(VERSION 3.20)

add_library(bsp_status INTERFACE)

target_include_directories(bsp_status
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
