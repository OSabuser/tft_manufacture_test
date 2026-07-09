# mcuboot_port/bootutil_sources.cmake
#
# Общий список файлов bootutil (MCUboot) + TinyCrypt + ASN.1-парсер,
# используемых и реальным ARM-таргетом (firmware/bootloader), и host-тестами
# (tests/host/mcuboot_port/) — чтобы не дублировать ~13 путей в двух местах.
#
# Состав определён вручную (не через .cmake файлы NXP-порта — см.
# firmware/bootloader/PLAN.md, Фаза 2) под конкретный режим:
#   MCUBOOT_DIRECT_XIP + MCUBOOT_DIRECT_XIP_REVERT, MCUBOOT_IMAGE_NUMBER=1,
#   MCUBOOT_SIGN_EC256 + MCUBOOT_USE_TINYCRYPT, без encryption, без
#   measured-boot/shared-data (поэтому НЕ включены: boot_record.c, caps.c,
#   encrypted.c, image_rsa.c, image_ed25519.c, swap_move.c, swap_misc.c,
#   fault_injection_hardening_delay_rng_mbedtls.c — последний нужен только
#   профилю MCUBOOT_FIH_PROFILE_HIGH, мы используем LOW).
#
# swap_scratch.c — ВКЛЮЧЁН, несмотря на название: boot_read_image_header()
# внутри него обёрнут в `#if !defined(MCUBOOT_SWAP_USING_MOVE)` — то есть
# это и есть дефолтная (не swap-move) реализация чтения заголовка слота,
# которую loader.c вызывает безусловно для ЛЮБОГО режима, включая
# Direct-XIP. swap_move.c (со своей версией той же функции под
# MCUBOOT_SWAP_USING_MOVE) не нужен. Остальные функции swap_scratch.c
# (собственно swap-логика) не достижимы в Direct-XIP и вырезаются линкером
# ARM-таргета через --gc-sections; для host-тестов — мёртвый, но безобидный
# код.

set(MCUBOOT_OPENSOURCE_DIR ${CMAKE_SOURCE_DIR}/sdk/middleware/mcuboot_opensource)
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
    ${CMAKE_CURRENT_LIST_DIR} # sysflash.h, mcuboot_config.h, flash_map.h, flash_map_backend.h
)

# ------------------------------------------------------------------------
# Опции компиляции только для вендоренных исходников (не для нашего кода —
# main.c/cli.c/boot_select.c и т.д. компилируются с обычными warnings/ASan
# консьюмера):
#   -w                              — вендоренный код, не наш стиль/lint
#   -fno-sanitize=address,undefined — обход бага clang 22.1.8 (Homebrew):
#     -fsanitize=address,undefined ломает генерацию CFI-директив на
#     некоторых больших функциях bootutil (напр. loader.c::context_boot_go)
#     — "invalid CFI advance_loc expression" на этапе ассемблирования. Без
#     санитайзеров те же файлы собираются чисто — похоже на баг конкретной
#     версии тулчейна. Не применимо к arm-none-eabi-gcc (ARM-таргет не
#     использует ASan) — гейтим по Clang.
# ------------------------------------------------------------------------
set(MCUBOOT_VENDORED_COMPILE_OPTIONS -w)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  list(APPEND MCUBOOT_VENDORED_COMPILE_OPTIONS -fno-sanitize=address,undefined)
endif()
set_source_files_properties(${MCUBOOT_BOOTUTIL_SOURCES}
                            PROPERTIES COMPILE_OPTIONS
                                       "${MCUBOOT_VENDORED_COMPILE_OPTIONS}")
