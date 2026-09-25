/**
 * @file  dcd_exec.c
 * @brief Интерпретатор DCD — см. dcd_exec.h и i.MX RT1050 RM Rev.4 §9.7.2.
 */

#include "dcd_exec.h"

#include <stdbool.h>

/* ── Формат ────────────────────────────────────────────────────────────── */

#define DCD_TAG_HEADER  0xD2U
#define DCD_TAG_WRITE   0xCCU
#define DCD_TAG_CHECK   0xCFU
#define DCD_TAG_NOP     0xC0U
#define DCD_VERSION     0x41U

#define DCD_HDR_SIZE        4U /**< Заголовок DCD и заголовок команды — по 4 байта. */
#define DCD_WRITE_PAIR_SIZE 8U /**< address + value/mask. */
#define DCD_CHECK_LEN       12U /**< Check без count. */
#define DCD_CHECK_LEN_COUNT 16U /**< Check с count. */

#define DCD_PAR_BYTES_MASK 0x07U
#define DCD_PAR_FLAG_MASK  0x08U /**< bit3 "Mask" */
#define DCD_PAR_FLAG_SET   0x10U /**< bit4 "Set"  */

/* ── Разбор big-endian ─────────────────────────────────────────────────── */

static uint16_t be16(const uint8_t *p_src)
{
    return (uint16_t) (((uint16_t) p_src[0] << 8) | p_src[1]);
}

static uint32_t be32(const uint8_t *p_src)
{
    return ((uint32_t) p_src[0] << 24) | ((uint32_t) p_src[1] << 16) |
           ((uint32_t) p_src[2] << 8) | (uint32_t) p_src[3];
}

/* ── Валидация ─────────────────────────────────────────────────────────── */

static bool width_is_valid(uint8_t width)
{
    return (width == 1U) || (width == 2U) || (width == 4U);
}

/** Адрес выровнен по ширине и значение/маска помещается в ширину (RM, NOTE к §9.7.2.1/2). */
static bool access_is_valid(uint32_t addr, uint32_t value, uint8_t width)
{
    if ((addr % width) != 0U)
    {
        return false;
    }
    return (width == 4U) || ((value >> (width * 8U)) == 0U);
}

static bsp_status_t validate_write(const uint8_t *p_cmd, uint16_t len)
{
    const uint8_t WIDTH = p_cmd[3] & DCD_PAR_BYTES_MASK;
    const uint16_t BODY = (uint16_t) (len - DCD_HDR_SIZE);

    if (!width_is_valid(WIDTH) || (BODY == 0U) || ((BODY % DCD_WRITE_PAIR_SIZE) != 0U))
    {
        return BSP_ERR_INVALID;
    }
    for (uint16_t i = DCD_HDR_SIZE; i < len; i += DCD_WRITE_PAIR_SIZE)
    {
        if (!access_is_valid(be32(&p_cmd[i]), be32(&p_cmd[i + 4U]), WIDTH))
        {
            return BSP_ERR_INVALID;
        }
    }
    return BSP_OK;
}

static bsp_status_t validate_check(const uint8_t *p_cmd, uint16_t len)
{
    const uint8_t WIDTH = p_cmd[3] & DCD_PAR_BYTES_MASK;

    if (!width_is_valid(WIDTH) || ((len != DCD_CHECK_LEN) && (len != DCD_CHECK_LEN_COUNT)))
    {
        return BSP_ERR_INVALID;
    }
    return access_is_valid(be32(&p_cmd[4]), be32(&p_cmd[8]), WIDTH) ? BSP_OK : BSP_ERR_INVALID;
}

static bsp_status_t validate_command(const uint8_t *p_cmd, uint16_t len)
{
    switch (p_cmd[0])
    {
    case DCD_TAG_WRITE:
        return validate_write(p_cmd, len);
    case DCD_TAG_CHECK:
        return validate_check(p_cmd, len);
    case DCD_TAG_NOP:
        return (len == DCD_HDR_SIZE) ? BSP_OK : BSP_ERR_INVALID;
    default:
        return BSP_ERR_NOT_SUPPORTED;
    }
}

/** Проверить заголовок, вернуть длину DCD из него (0 — заголовок невалиден). */
static uint16_t header_length(const uint8_t *p_dcd, size_t size)
{
    if ((size < DCD_HDR_SIZE) || (p_dcd[0] != DCD_TAG_HEADER) || (p_dcd[3] != DCD_VERSION))
    {
        return 0U;
    }
    const uint16_t LEN = be16(&p_dcd[1]);
    if ((LEN < DCD_HDR_SIZE) || (LEN > DCD_EXEC_MAX_SIZE) || (LEN > size))
    {
        return 0U;
    }
    return LEN;
}

static void result_set(dcd_exec_result_t *p_result, size_t offset, uint32_t commands,
                       uint32_t writes)
{
    if (p_result != NULL)
    {
        p_result->offset   = offset;
        p_result->commands = commands;
        p_result->writes   = writes;
    }
}

bsp_status_t dcd_exec_validate(const uint8_t *p_dcd, size_t size, dcd_exec_result_t *p_result)
{
    result_set(p_result, 0U, 0U, 0U);
    if (p_dcd == NULL)
    {
        return BSP_ERR_PARAM;
    }

    const uint16_t DCD_LEN = header_length(p_dcd, size);
    if (DCD_LEN == 0U)
    {
        return BSP_ERR_INVALID;
    }

    uint32_t commands = 0U;
    uint16_t offset   = DCD_HDR_SIZE;
    while (offset < DCD_LEN)
    {
        if ((DCD_LEN - offset) < DCD_HDR_SIZE)
        {
            result_set(p_result, offset, commands, 0U);
            return BSP_ERR_INVALID;
        }
        const uint16_t CMD_LEN = be16(&p_dcd[offset + 1U]);
        if ((CMD_LEN < DCD_HDR_SIZE) || (CMD_LEN > (DCD_LEN - offset)))
        {
            result_set(p_result, offset, commands, 0U);
            return BSP_ERR_INVALID;
        }
        const bsp_status_t STATUS = validate_command(&p_dcd[offset], CMD_LEN);
        if (STATUS != BSP_OK)
        {
            result_set(p_result, offset, commands, 0U);
            return STATUS;
        }
        commands++;
        offset = (uint16_t) (offset + CMD_LEN);
    }

    result_set(p_result, DCD_LEN, commands, 0U);
    return BSP_OK;
}

/* ── Исполнение ────────────────────────────────────────────────────────── */

/** RM табл. 9-41. Возвращает число выполненных записей. */
static uint32_t exec_write(const uint8_t *p_cmd, uint16_t len, uint16_t cmd_offset,
                           const dcd_exec_io_t *p_io)
{
    const uint8_t PAR   = p_cmd[3];
    const uint8_t WIDTH = PAR & DCD_PAR_BYTES_MASK;
    uint32_t writes     = 0U;

    for (uint16_t i = DCD_HDR_SIZE; i < len; i += DCD_WRITE_PAIR_SIZE)
    {
        const uint32_t ADDR  = be32(&p_cmd[i]);
        const uint32_t VALUE = be32(&p_cmd[i + 4U]);
        uint32_t out         = VALUE;

        if (p_io->on_command != NULL)
        {
            p_io->on_command((size_t) cmd_offset + i);
        }

        if ((PAR & DCD_PAR_FLAG_MASK) != 0U)
        {
            const uint32_t CUR = p_io->read(ADDR, WIDTH);
            out = ((PAR & DCD_PAR_FLAG_SET) != 0U) ? (CUR | VALUE) : (CUR & ~VALUE);
        }
        p_io->write(ADDR, out, WIDTH);
        writes++;
    }
    return writes;
}

/** RM табл. 9-45. */
static bool check_condition(uint8_t par, uint32_t value, uint32_t mask)
{
    const uint32_t MASKED = value & mask;
    const bool FLAG_MASK  = (par & DCD_PAR_FLAG_MASK) != 0U;
    const bool FLAG_SET   = (par & DCD_PAR_FLAG_SET) != 0U;

    if (!FLAG_MASK)
    {
        return FLAG_SET ? (MASKED == mask) : (MASKED == 0U); /* all set / all clear */
    }
    return FLAG_SET ? (MASKED != 0U) : (MASKED != mask); /* any set / any clear */
}

static bsp_status_t exec_check(const uint8_t *p_cmd, uint16_t len, const dcd_exec_io_t *p_io)
{
    const uint8_t PAR      = p_cmd[3];
    const uint8_t WIDTH    = PAR & DCD_PAR_BYTES_MASK;
    const uint32_t ADDR    = be32(&p_cmd[4]);
    const uint32_t MASK    = be32(&p_cmd[8]);
    const bool HAS_COUNT   = (len == DCD_CHECK_LEN_COUNT);
    const uint32_t COUNT   = HAS_COUNT ? be32(&p_cmd[12]) : 0U;
    const uint32_t START   = p_io->now_ms();
    uint32_t polls         = 0U;

    if (HAS_COUNT && (COUNT == 0U))
    {
        return BSP_OK; /* RM: count = 0 — ведёт себя как NOP */
    }

    for (;;)
    {
        if (check_condition(PAR, p_io->read(ADDR, WIDTH), MASK))
        {
            return BSP_OK;
        }
        polls++;
        if (HAS_COUNT && (polls >= COUNT))
        {
            return BSP_ERR_TIMEOUT;
        }
        if ((p_io->now_ms() - START) >= p_io->check_timeout_ms)
        {
            return BSP_ERR_TIMEOUT;
        }
    }
}

static bool io_is_valid(const dcd_exec_io_t *p_io)
{
    return (p_io != NULL) && (p_io->read != NULL) && (p_io->write != NULL) &&
           (p_io->now_ms != NULL);
}

bsp_status_t dcd_exec_run(const uint8_t *p_dcd, size_t size, const dcd_exec_io_t *p_io,
                          dcd_exec_result_t *p_result)
{
    if (!io_is_valid(p_io))
    {
        result_set(p_result, 0U, 0U, 0U);
        return BSP_ERR_PARAM;
    }

    const bsp_status_t VALID = dcd_exec_validate(p_dcd, size, p_result);
    if (VALID != BSP_OK)
    {
        return VALID;
    }

    const uint16_t DCD_LEN = be16(&p_dcd[1]);
    uint32_t commands      = 0U;
    uint32_t writes        = 0U;
    uint16_t offset        = DCD_HDR_SIZE;

    while (offset < DCD_LEN)
    {
        const uint8_t *const P_CMD = &p_dcd[offset];
        const uint16_t CMD_LEN     = be16(&P_CMD[1]);

        if ((p_io->on_command != NULL) && (P_CMD[0] != DCD_TAG_WRITE))
        {
            p_io->on_command(offset); /* write трассирует каждую пару сам */
        }

        if (P_CMD[0] == DCD_TAG_WRITE)
        {
            writes += exec_write(P_CMD, CMD_LEN, offset, p_io);
        }
        else if ((P_CMD[0] == DCD_TAG_CHECK) && (exec_check(P_CMD, CMD_LEN, p_io) != BSP_OK))
        {
            result_set(p_result, offset, commands, writes);
            return BSP_ERR_TIMEOUT;
        }
        else
        {
            /* NOP или выполненная Check */
        }
        commands++;
        offset = (uint16_t) (offset + CMD_LEN);
    }

    result_set(p_result, DCD_LEN, commands, writes);
    return BSP_OK;
}
