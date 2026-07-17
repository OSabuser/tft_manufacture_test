# mcuboot_port/bootutil_sources.cmake
#
# Общий список файлов bootutil (MCUboot) + TinyCrypt + ASN.1-парсер,
# используемых и реальным ARM-таргетом (firmware/bootloader), и host-тестами
# (tests/host/mcuboot_port/)

set(MCUBOOT_OPENSOURCE_DIR
    ${CMAKE_SOURCE_DIR}/sdk/middleware/mcuboot_opensource)
set(MCUBOOT_BOOTUTIL_DIR ${MCUBOOT_OPENSOURCE_DIR}/boot/bootutil)
set(MCUBOOT_EXT_DIR ${MCUBOOT_OPENSOURCE_DIR}/ext)

set(MCUBOOT_BOOTUTIL_SOURCES
    ${MCUBOOT_BOOTUTIL_DIR}/src/loader.c
    ${MCUBOOT_BOOTUTIL_DIR}/src/bootutil_misc.c
    ${MCUBOOT_BOOTUTIL_DIR}/src/bootutil_public.c
    ${MCUBOOT_BOOTUTIL_DIR}/src/tlv.c
    ${MCUBOOT_BOOTUTIL_DIR}/src/image_validate.c
    ${MCUBOOT_BOOTUTIL_DIR}/src/image_ecdsa.c
    ${MCUBOOT_BOOTUTIL_DIR}/src/fault_injection_hardening.c
    ${MCUBOOT_BOOTUTIL_DIR}/src/swap_scratch.c
    ${MCUBOOT_EXT_DIR}/tinycrypt/lib/source/ecc.c
    ${MCUBOOT_EXT_DIR}/tinycrypt/lib/source/ecc_dsa.c
    ${MCUBOOT_EXT_DIR}/tinycrypt/lib/source/sha256.c
    ${MCUBOOT_EXT_DIR}/tinycrypt/lib/source/utils.c
    ${MCUBOOT_EXT_DIR}/mbedtls-asn1/src/asn1parse.c
    ${MCUBOOT_EXT_DIR}/mbedtls-asn1/src/platform_util.c)

set(MCUBOOT_BOOTUTIL_INCLUDES
    ${MCUBOOT_BOOTUTIL_DIR}/include
    ${MCUBOOT_BOOTUTIL_DIR}/src
    ${MCUBOOT_EXT_DIR}/tinycrypt/lib/include
    ${MCUBOOT_EXT_DIR}/mbedtls-asn1/include
    ${CMAKE_CURRENT_LIST_DIR} # sysflash.h, mcuboot_config.h, flash_map.h,
                              # flash_map_backend.h
)

set(MCUBOOT_VENDORED_COMPILE_OPTIONS -w)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  list(APPEND MCUBOOT_VENDORED_COMPILE_OPTIONS -fno-sanitize=address,undefined)
endif()
set_source_files_properties(
  ${MCUBOOT_BOOTUTIL_SOURCES} PROPERTIES COMPILE_OPTIONS
                                         "${MCUBOOT_VENDORED_COMPILE_OPTIONS}")
