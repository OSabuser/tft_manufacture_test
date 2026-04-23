/**
 * @file  qspi.c
 * @brief BSP QSPI — W25Q64/128/256/512 через FlexSPI1.
 *
 * ## XIP-безопасность
 *
 * Прошивка исполняется XIP из Flash через FlexSPI AHB-интерфейс (слот 0).
 * Любая IP-команда FlexSPI прерывает AHB-путь на время выполнения.
 * Чтобы CPU не пытался фетчить инструкции из Flash во время IP-команды,
 * все функции, обращающиеся к регистрам FlexSPI, размещаются в ITCM через
 * QSPI_RAMFUNC. AHB prefetch отключается перед каждой
 * серией IP-транзакций и восстанавливается после.
 *
 * ## Стратегия адресации W25Q256/512
 *
 * Слот 0 FDCB использует 24-bit адресацию на всех чипах. Чтобы не сломать
 * XIP, режим адресации чипа (команда 0xB7) не переключается. Вместо этого
 * для W25Q256/512 загружаются dedicated 4-byte address opcodes:
 *   - Sector Erase: 0x20 → 0x21
 *   - Block Erase 32KB: 0x52 → 0x5C
 *   - Block Erase 64KB: 0xD8 → 0xDC
 *   - Quad Page Program: 0x32 → 0x34
 *   - IP Quad Output Read: 0x6B → 0x6C
 * Эти opcodes принимают 32-bit адрес независимо от текущего режима чипа.
 *
 * ## LUT-слоты
 *
 *   Slot  0 — XIP quad read       (FDCB, НЕ трогается)
 *   Slot  1 — Read Status Reg-1   (0x05)
 *   Slot  2 — Write Enable        (0x06)
 *   Slot  3 — Sector Erase 4KB    (0x20 / 0x21)
 *   Slot  4 — Quad Page Program   (0x32 / 0x34)
 *   Slot  5 — Read JEDEC ID       (0x9F)
 *   Slot  6 — Read Status Reg-2   (0x35)
 *   Slot  7 — Write Status Reg-2  (0x31)
 *   Slot  8 — Read Status Reg-3   (0x15)
 *   Slot  9 — Block Erase 32KB    (0x52 / 0x5C)
 *   Slot 10 — Block Erase 64KB    (0xD8 / 0xDC)
 *   Slot 11 — IP Quad Output Read (0x6B / 0x6C)
 */

#include "bsp/qspi_flash.h"

#include "fsl_common.h"
#include "fsl_flexspi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Привязка к железу ──────────────────────────────────────────────────── */

/** @brief FlexSPI instance для W25Qxx. */
#define QSPI_BASE FLEXSPI
/** @brief FlexSPI порт, к которому подключён W25Qxx. */
#define QSPI_PORT kFLEXSPI_PortA1

/* ── Индексы LUT-слотов ─────────────────────────────────────────────────── */

#define LSEQ_READ_SR1  1U  /**< Read Status Register 1 (0x05) */
#define LSEQ_WR_EN     2U  /**< Write Enable           (0x06) */
#define LSEQ_ERASE_4K  3U  /**< Sector Erase 4KB              */
#define LSEQ_PP_QUAD   4U  /**< Quad Page Program             */
#define LSEQ_JEDEC     5U  /**< Read JEDEC ID          (0x9F) */
#define LSEQ_READ_SR2  6U  /**< Read Status Register 2 (0x35) */
#define LSEQ_WR_SR2    7U  /**< Write Status Register 2(0x31) */
#define LSEQ_READ_SR3  8U  /**< Read Status Register 3 (0x15) */
#define LSEQ_ERASE_32K 9U  /**< Block Erase 32KB              */
#define LSEQ_ERASE_64K 10U /**< Block Erase 64KB              */
#define LSEQ_IP_READ   11U /**< IP Quad Output Read           */

/** @brief Первый слот BSP (слот 0 = XIP FDCB). */
#define LUT_BSP_FIRST_SLOT 1U
/** @brief Количество слотов BSP (1..11). */
#define LUT_BSP_SLOT_COUNT 11U
/** @brief Слов на одну LUT-последовательность. */
#define LUT_WORDS_PER_SEQ 4U
/** @brief Всего слов в таблице (слоты 0-11). */
#define LUT_TOTAL_WORDS ((LSEQ_IP_READ + 1U) * LUT_WORDS_PER_SEQ)

/* ── Адресация ──────────────────────────────────────────────────────────── */

/** @brief 3-byte адресация: 24-bit (W25Q64/128, стандартные opcodes). */
#define ADDR_BITS_24 0x18U
/**
 * @brief 4-byte адресация: 32-bit (W25Q256/512, dedicated opcodes).
 *
 * Используется с opcode-вариантами, принимающими 32-bit адрес без
 * переключения режима адресации чипа командой 0xB7.
 */
#define ADDR_BITS_32 0x20U

/* ── Регистровые константы ──────────────────────────────────────────────── */

#define QSPI_LUT_KEY      0x5AF05AF0UL /**< Ключ для разблокировки LUT. */
#define QSPI_LUTCR_UNLOCK 0x02U        /**< LUTCR: unlock.              */
#define QSPI_LUTCR_LOCK   0x01U        /**< LUTCR: lock.                */

/* ── Биты статусных регистров ───────────────────────────────────────────── */

#define SR1_BUSY_BIT 0x01U /**< SR1.WIP — Write In Progress. */
#define SR2_QE_BIT   0x02U /**< SR2.QE  — Quad Enable.       */

/* ── Параметры FIFO ─────────────────────────────────────────────────────── */

/** @brief Байт в одном слове RFDR/TFDR. */
#define QSPI_RFDR_WORD_BYTES 4U
/** @brief Байт в одном watermark-unit (по RM: 1 unit = 8 bytes). */
#define QSPI_WM_UNIT_BYTES 8U
/** @brief 32-bit слов в одном watermark-unit. */
#define QSPI_WM_UNIT_WORDS (QSPI_WM_UNIT_BYTES / QSPI_RFDR_WORD_BYTES)

/* ── Параметры SR-чтения ────────────────────────────────────────────────── */

/**
 * @brief Размер читаемого буфера для однобайтных SR-команд.
 *
 * FlexSPI FIFO работает минимальными единицами в 4 байта (RXWMRK=0, 1 FILL
 * unit = 4 bytes). Читаем 4 байта, используем только byte[0].
 */
#define SR_READ_LEN 4U

/* ── Размеры Flash по capacity byte ─────────────────────────────────────── */

#define QSPI_SIZE_8MB  0x00800000UL /**< W25Q64:  8 MB  */
#define QSPI_SIZE_16MB 0x01000000UL /**< W25Q128: 16 MB */
#define QSPI_SIZE_32MB 0x02000000UL /**< W25Q256: 32 MB */
#define QSPI_SIZE_64MB 0x04000000UL /**< W25Q512: 64 MB */

/* ── Macro: построение LUT-таблицы ─────────────────────────────────────── */

/*
 * Параметры:
 *   E4K   — opcode Sector Erase 4KB  (0x20 | 0x21)
 *   E32K  — opcode Block Erase 32KB  (0x52 | 0x5C)
 *   E64K  — opcode Block Erase 64KB  (0xD8 | 0xDC)
 *   PP    — opcode Quad Page Program  (0x32 | 0x34)
 *   RD    — opcode Quad Output Read   (0x6B | 0x6C)
 *   ABITS — биты адреса               (ADDR_BITS_24 | ADDR_BITS_32)
 *
 * Слот 0 = нули (XIP FDCB, не записывается в HW).
 * Слоты без data-фазы (Erase, WE) используют 1 word; PP и IP_READ — 2 words;
 * остальные words в слоте остаются нулями (STOP).
 */
/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
#define BUILD_LUT(E4K, E32K, E64K, PP, RD, ABITS)                                                  \
    {                                                                                              \
        [LUT_WORDS_PER_SEQ *                                                                       \
            LSEQ_READ_SR1] = FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x05U,           \
                                             kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04U),     \
            [LUT_WORDS_PER_SEQ * LSEQ_WR_EN] =                                                     \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x06U, kFLEXSPI_Command_STOP, \
                                kFLEXSPI_1PAD, 0x00U),                                             \
            [LUT_WORDS_PER_SEQ * LSEQ_ERASE_4K] =                                                  \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, (E4K),                        \
                                kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, (ABITS)),               \
            [LUT_WORDS_PER_SEQ * LSEQ_PP_QUAD] =                                                   \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, (PP),                         \
                                kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, (ABITS)),               \
            [LUT_WORDS_PER_SEQ * LSEQ_PP_QUAD + 1U] =                                              \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_4PAD, 0x04U,                  \
                                kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00U),                      \
            [LUT_WORDS_PER_SEQ * LSEQ_JEDEC] =                                                     \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x9FU,                        \
                                kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04U),                  \
            [LUT_WORDS_PER_SEQ * LSEQ_READ_SR2] =                                                  \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x35U,                        \
                                kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04U),                  \
            [LUT_WORDS_PER_SEQ * LSEQ_WR_SR2] =                                                    \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x31U,                        \
                                kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 0x04U),                 \
            [LUT_WORDS_PER_SEQ * LSEQ_READ_SR3] =                                                  \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x15U,                        \
                                kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x04U),                  \
            [LUT_WORDS_PER_SEQ * LSEQ_ERASE_32K] =                                                 \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, (E32K),                       \
                                kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, (ABITS)),               \
            [LUT_WORDS_PER_SEQ * LSEQ_ERASE_64K] =                                                 \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, (E64K),                       \
                                kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, (ABITS)),               \
            [LUT_WORDS_PER_SEQ * LSEQ_IP_READ] =                                                   \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, (RD),                         \
                                kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, (ABITS)),               \
            [LUT_WORDS_PER_SEQ * LSEQ_IP_READ + 1U] =                                              \
                FLEXSPI_LUT_SEQ(kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_4PAD, 0x08U,                  \
                                kFLEXSPI_Command_READ_SDR, kFLEXSPI_4PAD, 0x04U),                  \
    }

static const uint8_t BYTE_MASK = 0xFFU;

/** @brief LUT для W25Q64/128 — стандартные 3-byte opcodes. */
static const uint32_t K_LUT_3B[LUT_TOTAL_WORDS] =
    BUILD_LUT(0x20U, 0x52U, 0xD8U, 0x32U, 0x6BU, ADDR_BITS_24);

/** @brief LUT для W25Q256/512 — dedicated 4-byte opcodes. */
static const uint32_t K_LUT_4B[LUT_TOTAL_WORDS] =
    BUILD_LUT(0x21U, 0x5CU, 0xDCU, 0x34U, 0x6CU, ADDR_BITS_32);

/* ── Состояние модуля ───────────────────────────────────────────────────── */

static bool g_s_init           = false; /**< true после успешного init().  */
static uint32_t g_s_flash_size = 0U;    /**< Размер Flash в байтах.        */

/* ── ITCM: базовые примитивы FlexSPI ───────────────────────────────────── */

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static uint32_t qspi_irq_lock(void))
{
    const uint32_t PRIMASK = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    return PRIMASK;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static void qspi_irq_unlock(uint32_t primask))
{
    __DSB();
    __ISB();
    if ((primask & 1U) == 0U)
    {
        __enable_irq();
    }
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static void qspi_ahb_disable(void))
{
    QSPI_BASE->AHBCR &= ~FLEXSPI_AHBCR_PREFETCHEN_MASK;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static void qspi_ahb_enable(void))
{
    QSPI_BASE->AHBCR |= FLEXSPI_AHBCR_PREFETCHEN_MASK;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static void qspi_sw_reset(void))
{
    QSPI_BASE->MCR0 |= FLEXSPI_MCR0_SWRESET_MASK;
    while ((QSPI_BASE->MCR0 & FLEXSPI_MCR0_SWRESET_MASK) != 0U)
    {
    }
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static void qspi_wait_idle(void))
{
    while (((QSPI_BASE->STS0 & FLEXSPI_STS0_ARBIDLE_MASK) == 0U) ||
           ((QSPI_BASE->STS0 & FLEXSPI_STS0_SEQIDLE_MASK) == 0U))
    {
    }
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_check_error(uint32_t intr))
{
    const uint32_t ERR_FLAGS = (uint32_t) kFLEXSPI_SequenceExecutionTimeoutFlag |
                               (uint32_t) kFLEXSPI_IpCommandSequenceErrorFlag |
                               (uint32_t) kFLEXSPI_IpCommandGrantTimeoutFlag;

    const uint32_t ERR = intr & ERR_FLAGS;
    status_t result    = kStatus_Success;

    if (ERR != 0U)
    {
        if ((ERR & (uint32_t) kFLEXSPI_SequenceExecutionTimeoutFlag) != 0U)
        {
            result = kStatus_FLEXSPI_SequenceExecutionTimeout;
        }
        else if ((ERR & (uint32_t) kFLEXSPI_IpCommandSequenceErrorFlag) != 0U)
        {
            result = kStatus_FLEXSPI_IpCommandSequenceError;
        }
        else
        {
            result = kStatus_FLEXSPI_IpCommandGrantTimeout;
        }
        QSPI_BASE->INTR = ERR;
        QSPI_BASE->IPTXFCR |= FLEXSPI_IPTXFCR_CLRIPTXF_MASK;
        QSPI_BASE->IPRXFCR |= FLEXSPI_IPRXFCR_CLRIPRXF_MASK;
    }

    return result;
}

/* ── ITCM: RX FIFO ──────────────────────────────────────────────────────── */

/**
 * @brief Читает watermark-чанк из RX FIFO в p_dst.
 *
 * @param[in] p_dst     Буфер назначения.
 * @param[in] wm_words  Количество 32-bit слов в текущем watermark.
 */
/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_read_chunk(uint8_t *p_dst, uint32_t wm_words))
{
    uint32_t intr;
    status_t err;

    do
    {
        intr = QSPI_BASE->INTR;
        err  = qspi_check_error(intr);
        if (err != kStatus_Success)
        {
            return err;
        }
    } while ((intr & (uint32_t) kFLEXSPI_IpRxFifoWatermarkAvailableFlag) == 0U);

    /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) */
    for (uint32_t wm_word = 0U; wm_word < wm_words; wm_word++)
    {
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) */
        const uint32_t WORD = QSPI_BASE->RFDR[wm_word];
        const uint32_t BASE = wm_word * QSPI_RFDR_WORD_BYTES;
        p_dst[BASE + 0U]    = (uint8_t) (WORD & BYTE_MASK);
        p_dst[BASE + 1U]    = (uint8_t) ((WORD >> 8U) & BYTE_MASK);
        p_dst[BASE + 2U]    = (uint8_t) ((WORD >> 16U) & BYTE_MASK);
        p_dst[BASE + 3U]    = (uint8_t) ((WORD >> 24U) & BYTE_MASK);
    }
    QSPI_BASE->INTR = (uint32_t) kFLEXSPI_IpRxFifoWatermarkAvailableFlag;
    return kStatus_Success;
}

/**
 * @brief Читает tail-байты (< текущего watermark) из RX FIFO.
 *
 * Ожидает через IPRXFSTS.FILL.
 */
/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_read_tail(uint8_t *p_dst, uint32_t remain))
{
    const uint32_t WORDS_NEEDED = (remain + QSPI_RFDR_WORD_BYTES - 1U) / QSPI_RFDR_WORD_BYTES;
    bool done                   = false;

    while (!done)
    {
        const uint32_t FILL =
            (QSPI_BASE->IPRXFSTS & FLEXSPI_IPRXFSTS_FILL_MASK) >> FLEXSPI_IPRXFSTS_FILL_SHIFT;
        if (FILL >= WORDS_NEEDED)
        {
            done = true;
        }
        else
        {
            const status_t ERR = qspi_check_error(QSPI_BASE->INTR);
            if (ERR != kStatus_Success)
            {
                return ERR;
            }
        }
    }

    uint32_t bytes_left = remain;
    for (uint32_t word = 0U; word < WORDS_NEEDED; word++)
    {
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) */
        const uint32_t WORD = QSPI_BASE->RFDR[word];
        const uint32_t BYTES =
            (bytes_left < QSPI_RFDR_WORD_BYTES) ? bytes_left : QSPI_RFDR_WORD_BYTES;
        const uint32_t BASE = word * QSPI_RFDR_WORD_BYTES;
        for (uint32_t byte = 0U; byte < BYTES; byte++)
        {
            p_dst[BASE + byte] = (uint8_t) ((WORD >> (8U * byte)) & BYTE_MASK);
        }
        bytes_left -= BYTES;
    }
    QSPI_BASE->INTR = (uint32_t) kFLEXSPI_IpRxFifoWatermarkAvailableFlag;
    return kStatus_Success;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_read_fifo(uint8_t *p_buf, uint32_t len))
{
    const uint32_t WM_UNITS =
        ((QSPI_BASE->IPRXFCR & FLEXSPI_IPRXFCR_RXWMRK_MASK) >> FLEXSPI_IPRXFCR_RXWMRK_SHIFT) + 1U;
    const uint32_t WM_WORDS = WM_UNITS * QSPI_WM_UNIT_WORDS;
    const uint32_t WM_BYTES = WM_WORDS * QSPI_RFDR_WORD_BYTES;

    uint32_t offset = 0U;

    while ((len - offset) >= WM_BYTES)
    {
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
        const status_t ERR = qspi_read_chunk(p_buf + offset, WM_WORDS);
        if (ERR != kStatus_Success)
        {
            return ERR;
        }
        offset += WM_BYTES;
    }

    if (offset < len)
    {
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
        return qspi_read_tail(p_buf + offset, len - offset);
    }

    return kStatus_Success;
}

/* ── ITCM: TX FIFO ──────────────────────────────────────────────────────── */

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_write_fifo(const uint8_t *p_buf, uint32_t len))
{
    const uint8_t *p = p_buf;
    uint32_t remain  = len;
    const uint32_t WM_UNITS =
        ((QSPI_BASE->IPTXFCR & FLEXSPI_IPTXFCR_TXWMRK_MASK) >> FLEXSPI_IPTXFCR_TXWMRK_SHIFT) + 1U;
    const uint32_t WM_WORDS = WM_UNITS * QSPI_WM_UNIT_WORDS;
    const uint32_t WM_BYTES = WM_WORDS * QSPI_RFDR_WORD_BYTES;

    while (remain > 0U)
    {
        uint32_t intr;
        status_t err;
        do
        {
            intr = QSPI_BASE->INTR;
            err  = qspi_check_error(intr);
            if (err != kStatus_Success)
            {
                return err;
            }
        } while ((intr & (uint32_t) kFLEXSPI_IpTxFifoWatermarkEmptyFlag) == 0U);

        const uint32_t CHUNK    = (remain > WM_BYTES) ? WM_BYTES : remain;
        const uint32_t WR_WORDS = (CHUNK + QSPI_RFDR_WORD_BYTES - 1U) / QSPI_RFDR_WORD_BYTES;
        if (WR_WORDS > WM_WORDS)
        {
            return kStatus_FLEXSPI_IpCommandSequenceError;
        }

        for (uint32_t wr_word = 0U; wr_word < WR_WORDS; wr_word++)
        {
            const uint32_t OFF = wr_word * QSPI_RFDR_WORD_BYTES;
            const uint32_t BYTES =
                ((CHUNK - OFF) < QSPI_RFDR_WORD_BYTES) ? (CHUNK - OFF) : QSPI_RFDR_WORD_BYTES;
            uint32_t word = 0U;
            for (uint32_t byte = 0U; byte < BYTES; byte++)
            {
                word |= ((uint32_t) *p) << (8U * byte);
                p++;
            }
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) */
            QSPI_BASE->TFDR[wr_word] = word;
        }
        remain -= CHUNK;
        QSPI_BASE->INTR = (uint32_t) kFLEXSPI_IpTxFifoWatermarkEmptyFlag;
    }

    return kStatus_Success;
}

/* ── ITCM: LUT update ───────────────────────────────────────────────────── */

/**
 * @brief Записывает num_slots LUT-слотов начиная с first_slot.
 *
 * p_lut индексируется от слота 0 (слот 0 не записывается).
 */
/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static void qspi_lut_update(uint32_t first_slot, const uint32_t *p_lut,
                                                        uint32_t num_slots))
{
    qspi_wait_idle();

    QSPI_BASE->LUTKEY = QSPI_LUT_KEY;
    QSPI_BASE->LUTCR  = QSPI_LUTCR_UNLOCK;

    const uint32_t FIRST_WORD = first_slot * LUT_WORDS_PER_SEQ;
    const uint32_t WORD_COUNT = num_slots * LUT_WORDS_PER_SEQ;

    for (uint32_t i = 0U; i < WORD_COUNT; i++)
    {
        QSPI_BASE->LUT[FIRST_WORD + i] = p_lut[FIRST_WORD + i];
    }

    QSPI_BASE->LUTKEY = QSPI_LUT_KEY;
    QSPI_BASE->LUTCR  = QSPI_LUTCR_LOCK;
}

/* ── ITCM: IP-команды ───────────────────────────────────────────────────── */

/**
 * @brief Подготавливает IP-транзакцию (общая часть для read/write/cmd).
 */
/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static void qspi_ip_setup(uint32_t seq_idx, uint32_t addr,
                                                      uint32_t data_len))
{
    QSPI_BASE->FLSHCR2[QSPI_PORT] |= FLEXSPI_FLSHCR2_CLRINSTRPTR_MASK;
    QSPI_BASE->INTR = FLEXSPI_INTR_AHBCMDERR_MASK | FLEXSPI_INTR_IPCMDERR_MASK |
                      FLEXSPI_INTR_AHBCMDGE_MASK | FLEXSPI_INTR_IPCMDGE_MASK;
    QSPI_BASE->IPCR0 = addr;
    QSPI_BASE->IPTXFCR |= FLEXSPI_IPTXFCR_CLRIPTXF_MASK;
    QSPI_BASE->IPRXFCR |= FLEXSPI_IPRXFCR_CLRIPRXF_MASK;
    QSPI_BASE->IPCR1 =
        FLEXSPI_IPCR1_IDATSZ(data_len) | FLEXSPI_IPCR1_ISEQID(seq_idx) | FLEXSPI_IPCR1_ISEQNUM(0U);
    QSPI_BASE->IPCMD |= FLEXSPI_IPCMD_TRG_MASK;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_ip_read(uint32_t seq_idx, uint32_t addr,
                                                         uint8_t *p_rx, uint32_t data_len))
{
    qspi_ip_setup(seq_idx, addr, data_len);
    const status_t RESULT = qspi_read_fifo(p_rx, data_len);
    qspi_wait_idle();
    return RESULT;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_ip_write(uint32_t seq_idx, uint32_t addr,
                                                          const uint8_t *p_tx, uint32_t data_len))
{
    qspi_ip_setup(seq_idx, addr, data_len);
    const status_t RESULT = qspi_write_fifo(p_tx, data_len);
    qspi_wait_idle();
    return RESULT;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_ip_cmd(uint32_t seq_idx, uint32_t addr))
{
    qspi_ip_setup(seq_idx, addr, 0U);
    qspi_wait_idle();
    return qspi_check_error(QSPI_BASE->INTR);
}

/* ── ITCM: Flash management ─────────────────────────────────────────────── */

/**
 * @brief Ожидает снятия SR1.BUSY. Выполняется из ITCM на время erase/program.
 */
/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_wait_not_busy(void))
{
    uint8_t sr1_buf[SR_READ_LEN];
    status_t result;

    do
    {
        result = qspi_ip_read(LSEQ_READ_SR1, 0U, sr1_buf, SR_READ_LEN);
        if (result != kStatus_Success)
        {
            return result;
        }
    } while ((sr1_buf[0U] & SR1_BUSY_BIT) != 0U);

    return kStatus_Success;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_write_enable(uint32_t addr))
{
    return qspi_ip_cmd(LSEQ_WR_EN, addr);
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_read_sr(uint32_t seq_idx, uint8_t *p_val))
{
    uint8_t buf[SR_READ_LEN];
    const status_t RESULT = qspi_ip_read(seq_idx, 0U, buf, SR_READ_LEN);
    if (RESULT == kStatus_Success)
    {
        *p_val = buf[0U];
    }
    return RESULT;
}

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_write_sr2(uint8_t val))
{
    status_t result = qspi_write_enable(0U);
    if (result != kStatus_Success)
    {
        return result;
    }
    const uint8_t BUF[1U] = { val };
    result                = qspi_ip_write(LSEQ_WR_SR2, 0U, BUF, 1U);
    if (result != kStatus_Success)
    {
        return result;
    }
    return qspi_wait_not_busy();
}

/**
 * @brief Гарантирует установку SR2.QE (Quad Enable).
 *
 * Если бит не установлен — записывает SR2 через команду 0x31.
 */
/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
AT_QUICKACCESS_SECTION_CODE(static status_t qspi_ensure_quad_enabled(void))
{
    uint8_t sr2        = 0U;
    const status_t RES = qspi_read_sr(LSEQ_READ_SR2, &sr2);
    if (RES != kStatus_Success)
    {
        return RES;
    }
    if ((sr2 & SR2_QE_BIT) != 0U)
    {
        return kStatus_Success; /* Уже установлен */
    }
    return qspi_write_sr2((uint8_t) (sr2 | SR2_QE_BIT));
}

/* ── Вспомогательные функции (не ITCM) ─────────────────────────────────── */

AT_QUICKACCESS_SECTION_CODE(static bsp_status_t to_bsp(status_t s))
{
    return (s == (status_t) kStatus_Success) ? BSP_OK : BSP_ERR_HW;
}

/**
 * @brief Определяет параметры чипа по capacity byte JEDEC ID.
 *
 * @param[in]  cap_byte     Байт ёмкости (jedec_buf[2]).
 * @param[out] p_flash_size Размер Flash в байтах.
 * @param[out] p_lut        Указатель на подходящую LUT-таблицу.
 */
AT_QUICKACCESS_SECTION_CODE(static bsp_status_t qspi_detect_chip(uint8_t cap_byte,
                                                                 uint32_t *p_flash_size,
                                                                 const uint32_t **p_lut))
{
    switch (cap_byte)
    {
    case BSP_QSPI_CAP_64MBIT:
        *p_flash_size = QSPI_SIZE_8MB;
        *p_lut        = K_LUT_3B;
        break;
    case BSP_QSPI_CAP_128MBIT:
        *p_flash_size = QSPI_SIZE_16MB;
        *p_lut        = K_LUT_3B;
        break;
    case BSP_QSPI_CAP_256MBIT:
        *p_flash_size = QSPI_SIZE_32MB;
        *p_lut        = K_LUT_4B;
        break;
    case BSP_QSPI_CAP_512MBIT:
        *p_flash_size = QSPI_SIZE_64MB;
        *p_lut        = K_LUT_4B;
        break;
    default:
        return BSP_ERR_HW;
    }
    return BSP_OK;
}

/**
 * @brief Общая реализация erase-операций.
 *
 * Паттерн: AHBDisable → WriteEnable → IP cmd → WaitBusy → Reset → AHBEnable.
 */
AT_QUICKACCESS_SECTION_CODE(static bsp_status_t qspi_do_erase(uint32_t seq_idx, uint32_t addr))
{
    if (!g_s_init)
    {
        return BSP_ERR_HW;
    }
    const uint32_t PRIMASK = qspi_irq_lock();
    qspi_ahb_disable();

    status_t result = qspi_write_enable(addr);
    if (result == kStatus_Success)
    {
        result = qspi_ip_cmd(seq_idx, addr);
    }
    if (result == kStatus_Success)
    {
        result = qspi_wait_not_busy();
    }

    qspi_sw_reset();
    qspi_ahb_enable();
    qspi_irq_unlock(PRIMASK);
    return to_bsp(result);
}

/* ── Публичный API ──────────────────────────────────────────────────────── */

AT_QUICKACCESS_SECTION_CODE(bsp_status_t bsp_qspi_init(void))
{
    const uint32_t PRIMASK = qspi_irq_lock();
    bsp_status_t bsp_result;
    qspi_ahb_disable();

    /* Шаг 1: записываем JEDEC-слот (одинаков для всех чипов) */
    qspi_lut_update(LSEQ_JEDEC, K_LUT_3B, 1U);

    /* Шаг 2: читаем JEDEC ID */
    uint8_t jedec_buf[SR_READ_LEN] = { 0U };
    status_t result                = qspi_ip_read(LSEQ_JEDEC, 0U, jedec_buf, SR_READ_LEN);
    if (result != kStatus_Success)
    {
        bsp_result = BSP_ERR_HW;
        goto DONE;
    }

    /* Шаг 3: проверяем производителя */
    if (jedec_buf[0U] != BSP_QSPI_MFR_WINBOND)
    {
        bsp_result = BSP_ERR_HW;
        goto DONE;
    }

    /* Шаг 4: определяем чип и выбираем LUT */
    uint32_t flash_size       = 0U;
    const uint32_t *p_lut     = K_LUT_3B;
    const bsp_status_t DETECT = qspi_detect_chip(jedec_buf[2U], &flash_size, &p_lut);

    if (DETECT != BSP_OK)
    {
        bsp_result = BSP_ERR_HW;
        goto DONE;
    }

    /* Шаг 5: загружаем полный LUT (слоты 1-11) */
    qspi_lut_update(LUT_BSP_FIRST_SLOT, p_lut, LUT_BSP_SLOT_COUNT);

    /* Шаг 6: активируем Quad-режим */
    result = qspi_ensure_quad_enabled();

    if (result != kStatus_Success)
    {
        bsp_result = BSP_ERR_HW;
        goto DONE;
    }

    g_s_flash_size = flash_size;
    g_s_init       = true;
    bsp_result     = BSP_OK;

DONE:
    qspi_sw_reset();
    qspi_ahb_enable();
    qspi_irq_unlock(PRIMASK);
    return bsp_result;
}

AT_QUICKACCESS_SECTION_CODE(bsp_status_t bsp_qspi_read_jedec_id(bsp_qspi_jedec_t *p_jedec))
{
    if (p_jedec == NULL)
    {
        return BSP_ERR_HW;
    }
    const uint32_t PRIMASK = qspi_irq_lock();
    qspi_ahb_disable();

    uint8_t buf[SR_READ_LEN] = { 0U };
    const status_t RESULT    = qspi_ip_read(LSEQ_JEDEC, 0U, buf, SR_READ_LEN);

    qspi_sw_reset();
    qspi_ahb_enable();
    qspi_irq_unlock(PRIMASK);

    if (RESULT != kStatus_Success)
    {
        return BSP_ERR_HW;
    }
    p_jedec->manufacturer_id = buf[0U];
    p_jedec->device_id       = ((uint16_t) buf[1U] << 8U) | (uint16_t) buf[2U];
    return BSP_OK;
}

AT_QUICKACCESS_SECTION_CODE(bsp_status_t bsp_qspi_erase_sector(uint32_t addr))
{
    return qspi_do_erase(LSEQ_ERASE_4K, addr);
}

AT_QUICKACCESS_SECTION_CODE(bsp_status_t bsp_qspi_erase_block_32k(uint32_t addr))
{
    return qspi_do_erase(LSEQ_ERASE_32K, addr);
}

AT_QUICKACCESS_SECTION_CODE(bsp_status_t bsp_qspi_erase_block_64k(uint32_t addr))
{
    return qspi_do_erase(LSEQ_ERASE_64K, addr);
}

AT_QUICKACCESS_SECTION_CODE(bsp_status_t bsp_qspi_write_page(uint32_t addr, const uint8_t *p_data))
{
    if (!g_s_init || (p_data == NULL))
    {
        return BSP_ERR_HW;
    }
    const uint32_t PRIMASK = qspi_irq_lock();
    qspi_ahb_disable();

    status_t result = qspi_write_enable(addr);
    if (result == kStatus_Success)
    {
        result = qspi_ip_write(LSEQ_PP_QUAD, addr, p_data, BSP_QSPI_PAGE_SIZE);
    }
    if (result == kStatus_Success)
    {
        result = qspi_wait_not_busy();
    }

    qspi_sw_reset();
    qspi_ahb_enable();
    qspi_irq_unlock(PRIMASK);
    return to_bsp(result);
}

AT_QUICKACCESS_SECTION_CODE(bsp_status_t bsp_qspi_read(uint32_t addr, uint8_t *p_buf, size_t size))
{
    if (!g_s_init || (p_buf == NULL) || (size == 0U))
    {
        return BSP_ERR_HW;
    }
    const uint32_t PRIMASK = qspi_irq_lock();
    qspi_ahb_disable();

    /*
     * Используем слот LSEQ_IP_READ (11), настроенный с корректным opcode и
     * ABITS для данного чипа. Слот 0 (XIP FDCB) не используется — его
     * opcode и режим адресации зафиксированы FDCB и могут не совпадать
     * с тем, что нужно для IP-чтений (особенно для W25Q256/512).
     */
    const status_t RESULT = qspi_ip_read(LSEQ_IP_READ, addr, p_buf, (uint32_t) size);

    qspi_sw_reset();
    qspi_ahb_enable();
    qspi_irq_unlock(PRIMASK);
    return to_bsp(RESULT);
}

uint32_t bsp_qspi_flash_size(void)
{
    return g_s_flash_size;
}
