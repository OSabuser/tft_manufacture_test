/**
 * @file  partition.h
 * @brief Карта разделов QSPI NOR для tft_app (ARCH.md §10) — единый источник
 *        смещений для app и (в перспективе) генератора service_tui.
 *
 * Все смещения — БАЙТОВЫЕ, flash-относительные (0-based), как принимает
 * `bsp_qspi_*`. Размер-независимость: критичные регионы (загрузчик, слоты,
 * layout, settings) — по фиксированным смещениям; размер-зависим ТОЛЬКО регион
 * ассетов — стартует с фикс-адреса и тянется до конца чипа
 * (`длина = bsp_qspi_flash_size() - TFT_APP_QSPI_ASSETS_OFFSET`).
 *
 * Работает на W25Q128/256/512 без пересчёта: меняется лишь длина ассетов.
 */

#ifndef SERVICES_PARTITION_H_
#define SERVICES_PARTITION_H_

/* ── Фиксированные регионы (размер-независимо) ───────────────────────────── */

#define TFT_APP_QSPI_BOOTLOADER_OFFSET 0x000000U
#define TFT_APP_QSPI_BOOTLOADER_SIZE   0x040000U /* 256 КБ            */

#define TFT_APP_QSPI_SLOT_A_OFFSET     0x040000U
#define TFT_APP_QSPI_SLOT_B_OFFSET     0x240000U
#define TFT_APP_QSPI_SLOT_SIZE         0x200000U /* 2 МБ на слот      */

#define TFT_APP_QSPI_LAYOUT_OFFSET     0x440000U
#define TFT_APP_QSPI_LAYOUT_SIZE       0x010000U /* 64 КБ (+ client-UX TLV, ярус C) */

#define TFT_APP_QSPI_SETTINGS_OFFSET   0x450000U
#define TFT_APP_QSPI_SETTINGS_SIZE     0x002000U /* 8 КБ = 2 сектора:
                                                    сектор 0 — рабочий,
                                                    сектор 1 — задел под ping-pong */

/* ── Размер-зависимый регион ─────────────────────────────────────────────── */

#define TFT_APP_QSPI_ASSETS_OFFSET     0x452000U
/* Длина ассетов — рантайм: bsp_qspi_flash_size() - TFT_APP_QSPI_ASSETS_OFFSET. */

#endif /* SERVICES_PARTITION_H_ */
