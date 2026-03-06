option(SEGGER_RTT_ENABLED "Enable SEGGER RTT library" OFF)
option(UNITY_TESTING_ENABLED "Enable Unity testing framework" OFF)

# Создаём INTERFACE библиотеку для lib_external (агрегатор библиотек-субмодулей)
add_library(lib_external INTERFACE)

message(STATUS "==== Included external libraries ====")

# === SEGGER RTT ===
if(SEGGER_RTT_ENABLED)
  if(BUILD_TESTS_HOST)
    message(STATUS "SEGGER RTT ❎")
  else()
    add_subdirectory(SEGGER/wrapper)
    target_link_libraries(lib_external INTERFACE SeggerRTT)
    message(STATUS "SEGGER RTT ✅")
  endif()
else()
  message(STATUS "SEGGER RTT ❎")
endif()

# === UNITY Testing Framework ===
if(UNITY_TESTING_ENABLED)
  add_subdirectory(Unity)
  target_link_libraries(lib_external INTERFACE unity)
  message(STATUS "Unity ✅")
else()
  message(STATUS "Unity ❎")
endif()

# === FFF Mocking Framework ===
if(UNITY_TESTING_ENABLED)
  add_library(fff INTERFACE)
  target_include_directories(fff INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fff)
  target_link_libraries(lib_external INTERFACE fff)
  message(STATUS "FFF ✅")
else()
  message(STATUS "FFF ❎")
endif()

message(STATUS "==== ---------------- ====")
