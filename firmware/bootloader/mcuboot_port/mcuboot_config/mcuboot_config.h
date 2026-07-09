/**
 * @file  mcuboot_config.h
 * @brief Конфигурация bootutil (MCUboot) для загрузчика TFT.
 *
 * В отличие от шаблона NXP (nxp_mcux_sdk/include/mcuboot_config/mcuboot_config.h)
 * задаёт финальные макросы напрямую, без Kconfig-подобной прослойки —
 * решения зафиксированы в firmware/bootloader/PLAN.md, Фаза 2:
 *
 *   - Direct-XIP с revert: два слота, оба могут содержать валидный образ,
 *     bootutil выбирает более новую валидную версию; если она ни разу не
 *     подтверждена (confirm) — следующая загрузка откатится на предыдущую
 *     (см. MCUBOOT_DIRECT_XIP_REVERT).
 *   - ECDSA P-256 + TinyCrypt — компактный, полностью вендорен в репозитории
 *     (sdk/middleware/mcuboot_opensource/ext/tinycrypt), в отличие от
 *     mbedTLS (не вендорен, потребовал бы ~8000+ новых строк).
 *   - FIH профиль LOW — часть защиты bootutil (double-read сравнений) без
 *     RNG-задержки (та требует mbedTLS-энтропию, доступно только в профиле
 *     HIGH — не наш случай).
 *   - Heap НЕ нужен: malloc/free в bootutil (loader.c) вызываются только в
 *     swap-режиме, недостижимы под MCUBOOT_DIRECT_XIP.
 */

#ifndef MCUBOOT_CONFIG_H_
#define MCUBOOT_CONFIG_H_

/* ── Схема подписи ─────────────────────────────────────────────────────── */
#define MCUBOOT_SIGN_EC256

/* ── Крипто-бэкенд ─────────────────────────────────────────────────────── */
#define MCUBOOT_USE_TINYCRYPT

/* ── Режим обновления ─────────────────────────────────────────────────── */
#define MCUBOOT_DIRECT_XIP
#define MCUBOOT_DIRECT_XIP_REVERT

/* Проверять подпись активного слота при каждой загрузке, не только при
 * установке нового образа. */
#define MCUBOOT_VALIDATE_PRIMARY_SLOT

/* ── Образы ────────────────────────────────────────────────────────────── */
#define MCUBOOT_IMAGE_NUMBER 1

/* ── Flash-абстракция ─────────────────────────────────────────────────── */
#define MCUBOOT_USE_FLASH_AREA_GET_SECTORS

/* Slot A/Б = 2 МБ (см. docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md), сектор
 * W25Qxx = 4 КБ → 2 МБ / 4 КБ = 512 секторов на слот. */
#define MCUBOOT_MAX_IMG_SECTORS 512

/* ── Fault injection hardening ────────────────────────────────────────── */
#define MCUBOOT_FIH_PROFILE_LOW

/* ── Логирование — отключено, BOOT_LOG_* становятся no-op (bootutil_log.h) */

/* ── Watchdog — не используется в bootloader ─────────────────────────── */
#define MCUBOOT_WATCHDOG_FEED() \
    do                          \
    {                           \
    } while (0)

#endif /* MCUBOOT_CONFIG_H_ */
