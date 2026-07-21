/*
** ###################################################################
**     Linker file — tft_app как MCUboot-слот-образ (Direct-XIP).
**
**     Полный набор секций как в MIMXRT1052xxxxx_bootloader_flexspi_nor.ld
**     (включая .ram_function → ITCM для AT_QUICKACCESS_SECTION_CODE, .ncache,
**     DataQuickAccess), но адаптирован под слот:
**       - НЕТ .flash_config / .ivt (образ грузит не BootROM, а bootloader
**         software-jump'ом — FCB/IVT/DCD не нужны).
**       - Перед vector table зарезервирован imgtool-заголовок (0x200):
**         boot_select.c прыгает по flash_base + fa_off + ih_hdr_size.
**       - m_text — на весь слот (2 МБ − header − vectors).
**
**     КРИТИЧНО: .ram_function размещает код bsp_qspi_flash
**     (AT_QUICKACCESS_SECTION_CODE, входная секция CodeQuickAccess) в ITCM и
**     копируется startup'ом. Без неё этот код исполняется из flash, а любая
**     IP-команда FlexSPI (bsp_qspi_init/erase/write) блокирует AHB-путь →
**     HardFault на fetch следующей инструкции из той же flash (XIP). См.
**     bsp/qspi_flash/README.md "XIP-безопасность" и
**     docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md §5.
**
**     Базовый адрес слота — через -Wl,--defsym=__slot_base__=0x...
**       Slot A: 0x60040000   Slot Б: 0x60240000
**
**     Copyright 2016 Freescale Semiconductor, Inc.
**     Copyright 2016-2024 NXP
**     SPDX-License-Identifier: BSD-3-Clause
** ###################################################################
*/

ENTRY(Reset_Handler)

HEAP_SIZE       = DEFINED(__heap_size__)  ? __heap_size__  : 0x0400; /* newlib sbrk-heap; FreeRTOS heap_4 — свой массив в .bss */
STACK_SIZE      = DEFINED(__stack_size__) ? __stack_size__ : 0x1000;
VECTOR_RAM_SIZE = DEFINED(__ram_vector_table__) ? 0x00000400 : 0;

SLOT_BASE      = DEFINED(__slot_base__) ? __slot_base__ : 0x60040000;
SLOT_SIZE      = 0x00200000; /* 2 МБ — = imgtool -S 0x200000 и flash_map */
IMGTOOL_HDR_SZ = 0x200;      /* imgtool sign -H 0x200 */
VECTOR_SIZE    = 0x400;

MEMORY
{
  m_interrupts (RX) : ORIGIN = SLOT_BASE + IMGTOOL_HDR_SZ,               LENGTH = VECTOR_SIZE
  m_text       (RX) : ORIGIN = SLOT_BASE + IMGTOOL_HDR_SZ + VECTOR_SIZE, LENGTH = SLOT_SIZE - IMGTOOL_HDR_SZ - VECTOR_SIZE
  m_qacode     (RX) : ORIGIN = 0x00000000, LENGTH = 0x00020000 /* SRAM_ITC 128 КБ — ramfunc */
  m_data       (RW) : ORIGIN = 0x20000000, LENGTH = 0x00020000 /* SRAM_DTC 128 КБ — .data/.bss/heap/stack */
  m_data2      (RW) : ORIGIN = 0x20200000, LENGTH = 0x00040000 /* SRAM_OC 256 КБ — ncache */
  m_sdram      (RW) : ORIGIN = 0x80000000, LENGTH = 0x02000000 /* SDRAM 32 МБ — Фаза 0 не используется */
}

SECTIONS
{
  __NCACHE_REGION_START = ORIGIN(m_data2);
  __NCACHE_REGION_SIZE  = 0x2000; /* 8 КБ non-cacheable */

  /* Vector table (imgtool header зарезервирован до неё в MEMORY). */
  .interrupts :
  {
    __VECTOR_TABLE = .;
    __Vectors = .;
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > m_interrupts

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
  } > m_text

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > m_text

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > m_text

  .ctors :
  {
    __CTOR_LIST__ = .;
    KEEP (*crtbegin.o(.ctors))
    KEEP (*crtbegin?.o(.ctors))
    KEEP (*(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
    __CTOR_END__ = .;
  } > m_text

  .dtors :
  {
    __DTOR_LIST__ = .;
    KEEP (*crtbegin.o(.dtors))
    KEEP (*crtbegin?.o(.dtors))
    KEEP (*(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
    __DTOR_END__ = .;
  } > m_text

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } > m_text

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } > m_text

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } > m_text

  __etext = .;
  __DATA_ROM = .;

  .interrupts_ram :
  {
    . = ALIGN(4);
    __VECTOR_RAM__ = .;
    __interrupts_ram_start__ = .;
    *(.m_interrupts_ram)
    . += VECTOR_RAM_SIZE;
    . = ALIGN(4);
    __interrupts_ram_end__ = .;
  } > m_data

  __VECTOR_RAM = DEFINED(__ram_vector_table__) ? __VECTOR_RAM__ : ORIGIN(m_interrupts);
  __RAM_VECTOR_TABLE_SIZE_BYTES = DEFINED(__ram_vector_table__) ? (__interrupts_ram_end__ - __interrupts_ram_start__) : 0x0;

  .data : AT(__DATA_ROM)
  {
    . = ALIGN(4);
    __DATA_RAM = .;
    __data_start__ = .;
    *(.data)
    *(.data*)
    *(DataQuickAccess)       /* quick access data section */
    KEEP(*(.jcr*))
    . = ALIGN(4);
    __data_end__ = .;
  } > m_data

  __ram_function_flash_start = __DATA_ROM + (__data_end__ - __data_start__); /* startup копирует ramfunc из flash в ITCM */

  .ram_function : AT(__ram_function_flash_start)
  {
    . = ALIGN(32);
    __ram_function_start__ = .;
    *(CodeQuickAccess)       /* AT_QUICKACCESS_SECTION_CODE — bsp_qspi FlexSPI IP-код */
    . = ALIGN(128);
    __ram_function_end__ = .;
  } > m_qacode

  __NDATA_ROM = __ram_function_flash_start + (__ram_function_end__ - __ram_function_start__);
  .ncache.init :
  {
    . = ALIGN(32);
    __noncachedata_start__ = .;
    *(NonCacheable.init)
    . = ALIGN(4);
    __noncachedata_init_end__ = .;
  } > m_data2
  . = __noncachedata_init_end__;
  .ncache :
  {
    *(NonCacheable)
    . = ALIGN(4);
    __noncachedata_end__ = .;
  } > m_data2

  __DATA_END = __NDATA_ROM;
  text_end = ORIGIN(m_text) + LENGTH(m_text);
  ASSERT(__DATA_END <= text_end, "region m_text overflowed with text and data")
  ASSERT(text_end <= SLOT_BASE + SLOT_SIZE, "app вышел за пределы слота (2 МБ)")
  ASSERT((__noncachedata_end__ - ORIGIN(m_data2)) <= LENGTH(m_data2), "m_data2 ncache overflow")

  .bss :
  {
    . = ALIGN(4);
    __START_BSS = .;
    __bss_start__ = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
    __END_BSS = .;
  } > m_data

  .heap :
  {
    . = ALIGN(8);
    __end__ = .;
    PROVIDE(end = .);
    __HeapBase = .;
    . += HEAP_SIZE;
    __HeapLimit = .;
    __heap_limit = .;
  } > m_data

  .stack :
  {
    . = ALIGN(8);
    . += STACK_SIZE;
  } > m_data

  __StackTop   = ORIGIN(m_data) + LENGTH(m_data);
  __StackLimit = __StackTop - STACK_SIZE;
  PROVIDE(__stack = __StackTop);

  .ARM.attributes 0 : { *(.ARM.attributes) }

  ASSERT(__StackLimit >= __HeapLimit, "region m_data overflowed with stack and heap")
}
