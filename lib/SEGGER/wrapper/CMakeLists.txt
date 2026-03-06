cmake_minimum_required(VERSION 3.22)

option(SEGGER_RTT_ENABLED "Enable SEGGER RTT library" ON)

# Пути до исходников
set(RTT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../RTT/RTT)
set(RTT_SYSCALLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../RTT/Syscalls)
set(RTT_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../RTT/Config)

# Проверяем наличие субмодуля
if(NOT EXISTS "${RTT_SRC_DIR}/SEGGER_RTT.c")
  message(
    FATAL_ERROR
      "
❌ SEGGER_RTT submodule не инициализирован!

Запустите в корне проекта:
    git submodule update --init ThirdParty/SEGGER/RTT
    ")
endif()

# Создаём статическую библиотеку
add_library(SeggerRTT STATIC)

target_sources(
  SeggerRTT
  PRIVATE ${RTT_SRC_DIR}/SEGGER_RTT.c ${RTT_SRC_DIR}/SEGGER_RTT_printf.c
          ${RTT_SRC_DIR}/SEGGER_RTT_ASM_ARMv7M.S
          ${RTT_SYSCALLS_DIR}/SEGGER_RTT_Syscalls_GCC.c)

target_include_directories(
  SeggerRTT
  PUBLIC ${RTT_SRC_DIR} ${RTT_CONFIG_DIR}
  PRIVATE ${RTT_SYSCALLS_DIR})

target_compile_definitions(
  SeggerRTT
  PUBLIC SEGGER_RTT_MODE_DEFAULT=SEGGER_RTT_MODE_NO_BLOCK_SKIP
         SEGGER_RTT_MAX_NUM_UP_BUFFERS=3 SEGGER_RTT_BUFFER_SIZE_UP=1024)
