/**
 * @file  fake_flash_map_backend.h
 * @brief Test-only реализация контракта flash_map.h поверх обычной памяти
 *        хоста — заменяет bsp_qspi_flash для host-тестов bootutil (Direct-XIP
 *        выбор слота, см. firmware/bootloader/PLAN.md, Фаза 2).
 *
 * Слот A (fa_id=0) и слот Б (fa_id=1) — смежные регионы одного статического
 * буфера FAKE_SLOT_SIZE байт каждый, имитируют один flash-девайс с двумя
 * областями (как и на реальном железе — см. BOOTLOADER_FLASH_MAP.md).
 */

#ifndef FAKE_FLASH_MAP_BACKEND_H_
#define FAKE_FLASH_MAP_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

#define FAKE_FLASH_SECTOR_SIZE 0x1000U /* 4 KB — как реальный W25Qxx */
#define FAKE_FLASH_SLOT_SIZE   0x8000U /* 32 KB — уменьшенный тестовый слот */

/** @brief Заполнить всю fake-флеш 0xFF (оба слота — "стёрты"). */
void fake_flash_reset(void);

/**
 * @brief Записать содержимое (например, imgtool-подписанный фикстур-образ)
 *        в начало указанного слота.
 *
 * @param[in] slot_idx  0 — Slot A, 1 — Slot Б.
 * @param[in] p_data    Данные образа.
 * @param[in] len       Длина данных, <= FAKE_FLASH_SLOT_SIZE.
 */
void fake_flash_write_slot(int slot_idx, const uint8_t *p_data, size_t len);

#endif /* FAKE_FLASH_MAP_BACKEND_H_ */
