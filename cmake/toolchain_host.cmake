# Host GCC Toolchain — для сборки и запуска host-тестов (Unity + fff)
# Используется с пресетами: host-debug, host-release

# -----------------------------------------------------------------------------
# Нативная сборка — система и процессор хоста
# -----------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME ${CMAKE_HOST_SYSTEM_NAME})

# -----------------------------------------------------------------------------
# Компиляторы
# Clang доступен в devcontainer (clang-17), используем его для
# совместимости с clangd/clang-tidy. GCC как fallback.
# -----------------------------------------------------------------------------
find_program(HOST_C_COMPILER   NAMES clang-17 clang gcc)
find_program(HOST_CXX_COMPILER NAMES clang++-17 clang++ g++)

if(NOT HOST_C_COMPILER)
    message(FATAL_ERROR "Host C compiler not found. Install clang or gcc.")
endif()

set(CMAKE_C_COMPILER   "${HOST_C_COMPILER}")
set(CMAKE_CXX_COMPILER "${HOST_CXX_COMPILER}")

message(STATUS "Host C compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "Host CXX compiler: ${CMAKE_CXX_COMPILER}")

# -----------------------------------------------------------------------------
# Флаги по типу сборки
# -----------------------------------------------------------------------------
set(CMAKE_C_FLAGS_DEBUG     "-O0 -g3 -gdwarf-4" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g3 -gdwarf-4" CACHE INTERNAL "")

set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG"      CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG"      CACHE INTERNAL "")

# -----------------------------------------------------------------------------
# Покрытие кода — включается при Debug сборке для host-тестов
# Используется с lcov/gcovr для генерации отчётов
# -----------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS_DEBUG
        "${CMAKE_C_FLAGS_DEBUG} --coverage -fprofile-arcs -ftest-coverage"
        CACHE INTERNAL "")
    set(CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG} --coverage -fprofile-arcs -ftest-coverage"
        CACHE INTERNAL "")
endif()

# -----------------------------------------------------------------------------
# Санитайзеры для Debug сборки — помогают ловить UB и утечки памяти
# Отключи если тормозит или конфликтует с тестовым фреймворком
# -----------------------------------------------------------------------------
option(ENABLE_SANITIZERS "Enable AddressSanitizer + UBSanitizer" ON)

if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS_DEBUG
        "${CMAKE_C_FLAGS_DEBUG} ${SANITIZER_FLAGS}" CACHE INTERNAL "")
    set(CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG} ${SANITIZER_FLAGS}" CACHE INTERNAL "")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG
        "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${SANITIZER_FLAGS}" CACHE INTERNAL "")
    message(STATUS "Sanitizers: ON (Address + UB)")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")