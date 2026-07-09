/*
** ###################################################################
**     Linker file for the GNU C Compiler
**
**     Abstract:
**         Тестовая заглушка для аппаратной верификации Фазы 2 bootutil
**         (firmware/bootloader/PLAN.md) — минимальный XIP-образ, который
**         boot_go() выбирает и в который bootloader реально прыгает.
**         НЕ boot-образ для BootROM (нет FCB/IVT/DCD секций — Slot A/Б не
**         видны BootROM напрямую, только software jump из bootloader).
**
**         Базовый адрес слота передаётся через -Wl,--defsym=__slot_base__=0x...
**         (Slot A: 0x60040000, Slot Б: 0x60240000, см.
**         docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md). imgtool header (0x200)
**         зарезервирован перед vector table — boot_select.c вычисляет адрес
**         прыжка как flash_base + fa_off + ih_hdr_size.
** ###################################################################
*/

ENTRY(Reset_Handler)

HEAP_SIZE  = DEFINED(__heap_size__)  ? __heap_size__  : 0x400;
STACK_SIZE = DEFINED(__stack_size__) ? __stack_size__ : 0x400;

SLOT_BASE      = DEFINED(__slot_base__) ? __slot_base__ : 0x60040000;
IMGTOOL_HDR_SZ = 0x200; /* -H 0x200 при imgtool sign, см. PLAN.md */

MEMORY
{
  m_interrupts (RX) : ORIGIN = SLOT_BASE + IMGTOOL_HDR_SZ, LENGTH = 0x00000400
  m_text       (RX) : ORIGIN = SLOT_BASE + IMGTOOL_HDR_SZ + 0x400, LENGTH = 0x00008000 /* 32 KB — с большим запасом для мигалки */
  m_data       (RW) : ORIGIN = 0x20000000, LENGTH = 0x00020000 /* SRAM_DTC 128KB */
  m_data2      (RW) : ORIGIN = 0x20200000, LENGTH = 0x00040000 /* SRAM_OC 256KB — не используется стабом, нужна board_mpu_init() */
}

SECTIONS
{
  __NCACHE_REGION_START = ORIGIN(m_data2);
  __NCACHE_REGION_SIZE  = 0x2000; /* 8 KB non-cacheable, как в остальных линкер-скриптах */

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

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } > m_text

  __etext = .;
  __DATA_ROM = .;

  .data : AT(__DATA_ROM)
  {
    . = ALIGN(4);
    __data_start__ = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    __data_end__ = .;
  } > m_data

  .bss :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
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
