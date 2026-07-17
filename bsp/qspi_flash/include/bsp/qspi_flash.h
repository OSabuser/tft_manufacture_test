/**
 * @file  qspi_flash.h
 * @brief Публичный API BSP QSPI — W25Q64/128/256/512 через FlexSPI1.
 *
 * Поддерживаемые чипы Winbond:
 *
 * | Чип     | JEDEC cap | Размер | Адресация в IP-командах |
 * |---------|-----------|--------|-------------------------|
 * | W25Q64  | 0x17      |  8 MB  | 3-byte opcodes           |
 * | W25Q128 | 0x18      | 16 MB  | 3-byte opcodes           |
 * | W25Q256 | 0x19      | 32 MB  | Dedicated 4-byte opcodes |
 * | W25Q512 | 0x20      | 64 MB  | Dedicated 4-byte opcodes |
 *
 * Порядок вызова:
 *   1. bsp_qspi_init()  — детекция чипа, загрузка LUT.
 *   2. bsp_qspi_*()     — операции с Flash.
 *
 * XIP-безопасность:
 *   Слот 0 FlexSPI LUT (XIP read, задаётся FDCB) никогда не изменяется.
 *   Для W25Q256/512 используются dedicated 4-byte address opcodes —
 *   без переключения режима адресации чипа (0xB7), что гарантирует
 *   непрерывность XIP на всех поддерживаемых чипах.
 *
 * @note Этот заголовок не включает NXP SDK headers.
 */

#ifndef BSP_QSPI_H_
#define BSP_QSPI_H_

#include "bsp/status.h"

#include <stddef.h>
#include <stdint.h>

/* ── Производитель ─────────────────────────────────────────────────────── */

/** @brief JEDEC Manufacturer ID: Winbond Electronics. */
#define BSP_QSPI_MFR_WINBOND 0xEFU

/* ── Коды ёмкости (JEDEC device_id, младший байт) ─────────────────────── */

#define BSP_QSPI_CAP_64MBIT  0x17U /**< W25Q64,   8 MB  */
#define BSP_QSPI_CAP_128MBIT 0x18U /**< W25Q128, 16 MB  */
#define BSP_QSPI_CAP_256MBIT 0x19U /**< W25Q256, 32 MB  */
#define BSP_QSPI_CAP_512MBIT 0x20U /**< W25Q512, 64 MB  */

/* ── Геометрия Flash ───────────────────────────────────────────────────── */

/** @brief Размер страницы, байт. */
#define BSP_QSPI_PAGE_SIZE 256U

/** @brief Размер сектора 4 KB, байт. */
#define BSP_QSPI_SECTOR_SIZE 0x1000U

/** @brief Размер блока 32 KB, байт. */
#define BSP_QSPI_BLOCK_32K_SIZE 0x8000U

/** @brief Размер блока 64 KB, байт. */
#define BSP_QSPI_BLOCK_64K_SIZE 0x10000U

/* ── Типы ──────────────────────────────────────────────────────────────── */

/**
 * @brief JEDEC ID, прочитанный командой 0x9F.
 */
typedef struct
{
    uint8_t manufacturer_id; /**< Производитель (0xEF = Winbond).        */
    uint16_t device_id;      /**< [15:8]=тип памяти, [7:0]=код ёмкости. */
} bsp_qspi_jedec_t;

/* ── Публичный API ─────────────────────────────────────────────────────── */

/**
 * @brief Инициализация QSPI-подсистемы.
 *
 * Читает JEDEC ID (0x9F), определяет чип Winbond, загружает LUT-слоты 1-11
 * с opcode'ами, соответствующими режиму адресации чипа. Активирует Quad-
 * режим (SR2.QE). Слот 0 (XIP, задан FDCB) не изменяется.
 *
 * Должна быть вызвана до любой другой bsp_qspi_* функции.
 *
 * @retval BSP_OK   Чип опознан, LUT загружен, Quad включён.
 * @retval BSP_ERR  Ошибка FlexSPI или неизвестный/неподдерживаемый чип.
 */
bsp_status_t bsp_qspi_init(void);

/**
 * @brief Чтение JEDEC ID (команда 0x9F).
 *
 * @param[out] p_jedec  Результат. Не NULL.
 * @retval BSP_OK / BSP_ERR.
 *
 * @note Работает и после проваленного bsp_qspi_init() — LUT-слот для чтения
 *       JEDEC ID грузится безусловным первым шагом внутри него, до любой из
 *       проверок, на которых init() мог отвалиться. Полезно для диагностики
 *       "что именно распаяно", когда чип не опознан/не тот.
 */
bsp_status_t bsp_qspi_read_jedec_id(bsp_qspi_jedec_t *p_jedec);

/**
 * @brief Человекочитаемое имя и ёмкость чипа по capacity byte JEDEC ID.
 *
 * Тот же байт, что различает поддерживаемые чипы в bsp_qspi_init() — вынесен
 * отдельно, чтобы потребитель мог опознать чип из уже прочитанного
 * bsp_qspi_jedec_t.device_id, не завися от успеха bsp_qspi_init().
 *
 * @param[in]  cap_byte   Байт ёмкости (device_id & 0xFF).
 * @param[out] p_size_mb  Ёмкость чипа, МБ. 0, если байт не распознан.
 *                        Может быть NULL, если размер не нужен.
 * @return "W25Q64"/"W25Q128"/"W25Q256"/"W25Q512", либо "UNKNOWN" для
 *         нераспознанного байта.
 */
const char *bsp_qspi_decode_chip(uint8_t cap_byte, uint32_t *p_size_mb);

/**
 * @brief Стирание сектора 4 KB.
 *
 * Блокирующая операция (~45 мс типичное). Во время стирания Flash BUSY —
 * IP-команды записи/стирания недоступны, XIP-чтения работают.
 * Вызывающий код должен вызвать bsp_usb_cdc_poll() до и после.
 *
 * @param[in] addr  Байтовый flash-адрес. Должен быть выровнен на
 *                  BSP_QSPI_SECTOR_SIZE.
 * @retval BSP_OK / BSP_ERR.
 */
bsp_status_t bsp_qspi_erase_sector(uint32_t addr);

/**
 * @brief Стирание блока 32 KB.
 *
 * Блокирующая операция (~120 мс типичное, 1600 мс макс).
 * Оптимален для очистки областей кратных 32 KB.
 * Вызывающий код должен вызвать bsp_usb_cdc_poll() до и после.
 *
 * @param[in] addr  Байтовый flash-адрес. Должен быть выровнен на
 *                  BSP_QSPI_BLOCK_32K_SIZE.
 * @retval BSP_OK / BSP_ERR.
 */
bsp_status_t bsp_qspi_erase_block_32k(uint32_t addr);

/**
 * @brief Стирание блока 64 KB.
 *
 * Блокирующая операция (~150 мс типичное, 2000 мс макс).
 * Оптимальный выбор для очистки многомегабайтных областей:
 * 1 MB = 16 блоков × 150 мс ≈ 2.4 с (vs ~11.5 с через 4 KB секторы).
 * Вызывающий код должен вызвать bsp_usb_cdc_poll() до и после.
 *
 * @param[in] addr  Байтовый flash-адрес. Должен быть выровнен на
 *                  BSP_QSPI_BLOCK_64K_SIZE.
 * @retval BSP_OK / BSP_ERR.
 */
bsp_status_t bsp_qspi_erase_block_64k(uint32_t addr);

/**
 * @brief Запись одной страницы (256 байт).
 *
 * Блокирующая операция (~3 мс типичное).
 * Сектор должен быть предварительно стёрт.
 *
 * @param[in] addr    Байтовый flash-адрес. Должен быть выровнен на
 *                    BSP_QSPI_PAGE_SIZE.
 * @param[in] p_data  Указатель на BSP_QSPI_PAGE_SIZE байт. Не NULL.
 * @retval BSP_OK / BSP_ERR.
 */
bsp_status_t bsp_qspi_write_page(uint32_t addr, const uint8_t *p_data);

/**
 * @brief Чтение произвольного числа байт через IP-команду.
 *
 * Не использует AHB/XIP-путь. Корректно обрабатывает адреса выше 16 MB
 * для W25Q256/512 через dedicated 4-byte read opcode (0x6C).
 *
 * @param[in]  addr   Байтовый flash-адрес.
 * @param[out] p_buf  Буфер приёма. Не NULL.
 * @param[in]  size   Количество байт. > 0.
 * @retval BSP_OK / BSP_ERR.
 */
bsp_status_t bsp_qspi_read(uint32_t addr, uint8_t *p_buf, size_t size);

/**
 * @brief Возвращает размер Flash-чипа, определённый в bsp_qspi_init().
 *
 * @return Размер Flash в байтах, или 0 если init не была вызвана.
 */
uint32_t bsp_qspi_flash_size(void);

#endif /* BSP_QSPI_H_ */