cmake_minimum_required(VERSION 3.20)
project(tft_firmware C ASM)

# -----------------------------------------------------------------------------
# Глобальные опции — управляются через пресеты
# -----------------------------------------------------------------------------
option(BUILD_TESTS_HOST "Build host unit tests" OFF)
option(BUILD_TESTS_TARGET "Build on-target tests" OFF)
option(SEGGER_RTT_ENABLED "Enable SEGGER RTT" OFF)
option(UNITY_TESTING_ENABLED "Enable Unity framework" OFF)

# -----------------------------------------------------------------------------
# Общие настройки для ARM сборки
# -----------------------------------------------------------------------------
if(NOT BUILD_TESTS_HOST)
  set(CMAKE_EXECUTABLE_SUFFIX ".elf")
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
  set(BSP_SYSCALLS_FILE
      "${CMAKE_SOURCE_DIR}/bsp/generated/syscalls.c"
      CACHE FILEPATH "Заглушки системных вызовов newlib")
  set(BSP_GENERATED
      "${CMAKE_SOURCE_DIR}/bsp/generated"
      CACHE PATH "Путь до сгенерированных ConfigTools файлов")
  set(BSP_STARTUP_FILE
      "${BSP_GENERATED}/startup/startup_MIMXRT1052.S"
      CACHE FILEPATH "Путь до стартап файла")
endif()

# -----------------------------------------------------------------------------
# SDK и BSP — всегда подключаются, но содержат защиту от host сборки внутри
# -----------------------------------------------------------------------------
add_subdirectory(sdk)
add_subdirectory(utils)
add_subdirectory(bsp)
add_subdirectory(port)

# -----------------------------------------------------------------------------
# Сторонние и внутренние библиотеки
# -----------------------------------------------------------------------------
add_subdirectory(lib)

# -----------------------------------------------------------------------------
# Firmware проекты — только для ARM сборки
# -----------------------------------------------------------------------------
if(NOT BUILD_TESTS_HOST)
  add_subdirectory(firmware/test)
  # add_subdirectory(firmware/bootloader) - add_subdirectory(firmware/app) #
  # Загрузчик + Основное приложение
endif()

# -----------------------------------------------------------------------------
# Тесты
# -----------------------------------------------------------------------------
if(BUILD_TESTS_HOST OR BUILD_TESTS_TARGET)
  enable_testing()
  add_subdirectory(tests)
endif()
