# Copyright 2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

# TOOLCHAIN EXTENSION
if(WIN32)
  set(TOOLCHAIN_EXT ".exe")
else()
  set(TOOLCHAIN_EXT "")
endif()

# EXECUTABLE EXTENSION
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

# -----------------------------------------------------------------------------
# Путь к тулчейну В devcontainer: /opt/arm-toolchain (symlink на конкретную
# версию) Переопределяется через переменную окружения ARMGCC_DIR если нужно
# -----------------------------------------------------------------------------
if(DEFINED ENV{ARMGCC_DIR})
  set(TOOLCHAIN_DIR $ENV{ARMGCC_DIR})
  string(REGEX REPLACE "\\\\" "/" TOOLCHAIN_DIR "${TOOLCHAIN_DIR}")
else()
  set(TOOLCHAIN_DIR "/opt/arm-toolchain")
endif()

if(NOT EXISTS "${TOOLCHAIN_DIR}")
  message(
    FATAL_ERROR
      "ARM toolchain not found at: ${TOOLCHAIN_DIR}\n"
      "Set ARMGCC_DIR environment variable or install toolchain to /opt/arm-toolchain"
  )
endif()

message(STATUS "ARM Toolchain: ${TOOLCHAIN_DIR}")

# -----------------------------------------------------------------------------
# Target triplet и пути
# -----------------------------------------------------------------------------
set(TARGET_TRIPLET "arm-none-eabi")

set(TOOLCHAIN_BIN_DIR ${TOOLCHAIN_DIR}/bin)
set(TOOLCHAIN_INC_DIR ${TOOLCHAIN_DIR}/${TARGET_TRIPLET}/include)
set(TOOLCHAIN_LIB_DIR ${TOOLCHAIN_DIR}/${TARGET_TRIPLET}/lib)

# -----------------------------------------------------------------------------
# CMake система и процессор
# -----------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# -----------------------------------------------------------------------------
# Компиляторы
# -----------------------------------------------------------------------------
set(CMAKE_C_COMPILER ${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-gcc${TOOLCHAIN_EXT})
set(CMAKE_CXX_COMPILER
    ${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-g++${TOOLCHAIN_EXT})
set(CMAKE_ASM_COMPILER
    ${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-gcc${TOOLCHAIN_EXT})

set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

# -----------------------------------------------------------------------------
# Утилиты
# -----------------------------------------------------------------------------
set(CMAKE_OBJCOPY
    "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-objcopy"
    CACHE INTERNAL "objcopy")
set(CMAKE_OBJDUMP
    "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-objdump"
    CACHE INTERNAL "objdump")
set(CMAKE_SIZE
    "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-size"
    CACHE INTERNAL "size")

# -----------------------------------------------------------------------------
# Флаги процессора — MIMXRT1052 (Cortex-M7, FPv5-D16, hard-float ABI)
# -----------------------------------------------------------------------------
set(CPU_FLAGS
    "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections"
)
set(CMAKE_C_FLAGS
    "${CMAKE_C_FLAGS}   ${CPU_FLAGS}"
    CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} ${CPU_FLAGS}"
    CACHE INTERNAL "")
set(CMAKE_ASM_FLAGS
    "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}"
    CACHE INTERNAL "")

# -----------------------------------------------------------------------------
# Флаги по типу сборки
# -----------------------------------------------------------------------------
set(CMAKE_C_FLAGS_DEBUG
    "-O0 -g3 -gdwarf-4"
    CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_DEBUG
    "-O0 -g3 -gdwarf-4"
    CACHE INTERNAL "")
set(CMAKE_ASM_FLAGS_DEBUG
    "-g"
    CACHE INTERNAL "")

set(CMAKE_C_FLAGS_RELEASE
    "-O3 -DNDEBUG"
    CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_RELEASE
    "-O3 -DNDEBUG"
    CACHE INTERNAL "")
set(CMAKE_ASM_FLAGS_RELEASE
    ""
    CACHE INTERNAL "")

# -----------------------------------------------------------------------------
# C runtime библиотека — newlib-nano (меньше размер, подходит для embedded)
# -----------------------------------------------------------------------------
# nano.specs — выбирает компактную версию newlib-nano (меньший размер printf,
# malloc и т.д.). Но не предоставляет реализацию системных вызовов.

# nosys.specs — предоставляет именно заглушки syscalls. Без него newlib ожидает
# что ты сам реализуешь _sbrk, _write, _exit и т.д. — либо через semihosting,
# либо вручную.

# --gc-sections — убирает неиспользуемые секции кода и данных. Работает в паре с
# -ffunction-sections -fdata-sections у компилятора (каждая функция в отдельной
# секции — линкер выбрасывает ненужные).
set(CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nano.specs -Wl,--no-warn-rwx-segments")

# -----------------------------------------------------------------------------
# Общие флаги линкера
# -----------------------------------------------------------------------------
set(CMAKE_EXE_LINKER_FLAGS_DEBUG
    ""
    CACHE INTERNAL "")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE
    ""
    CACHE INTERNAL "")

# -----------------------------------------------------------------------------
# Поиск библиотек только в тулчейне, не на хосте
# -----------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_DIR}/${TARGET_TRIPLET} ${EXTRA_FIND_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

message(STATUS "Build type: " ${CMAKE_BUILD_TYPE})
