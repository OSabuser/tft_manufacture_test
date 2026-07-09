/**
 * @file  keys.c
 * @brief Таблица публичных ключей bootutil.
 *
 * MCUBOOT_HW_KEY / MCUBOOT_BUILTIN_KEY не определены (см. mcuboot_config.h)
 * — bootutil использует стандартный путь: TLV образа несёт хэш ключа,
 * bootutil ищет совпадение в bootutil_keys[] и проверяет подпись найденным
 * ключом. По образцу sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/keys.c.
 */

#include <bootutil/sign_key.h>
#include <mcuboot_config/mcuboot_config.h>

#if defined(MCUBOOT_SIGN_EC256)
#include "keys/bootloader_test_ecdsa_pub.c"
#else
#error "No public key available for given signing algorithm."
#endif

const struct bootutil_key bootutil_keys[] = {
    {
        .key = ecdsa_pub_key,
        .len = &ecdsa_pub_key_len,
    },
};

const int bootutil_key_cnt = 1;
