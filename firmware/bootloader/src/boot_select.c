/**
 * @file  boot_select.c
 * @brief bootutil boot_go() + прыжок в выбранный образ.
 *
 * Референс — sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/boot.c::do_boot():
 * та же последовательность (flash_device_base → вычислить адрес vector table →
 * cleanup → __set_MSP → __ISB → прыжок на Reset_Handler), CMSIS-интринсики, без
 * ассемблера. cleanup_before_jump()/VTOR добавлены в Фазе 3 — см. её docstring
 * про найденный на железе баг (прыжок сразу после bsp_usb_cdc_init()).
 */

#include "boot_select.h"

#include "bootutil/bootutil.h"
#include "bootutil/fault_injection_hardening.h"
#include "flash_map.h"
#include "fsl_common.h"

struct arm_vector_table
{
    uint32_t msp;
    uint32_t reset;
};

/**
 * @brief Вернуть NVIC/SysTick в состояние "как после аппаратного сброса"
 *        перед прыжком — целевой образ не должен унаследовать прерывания,
 *        включённые bootloader'ом.
 *
 */
static void cleanup_before_jump(void)
{
    for (uint32_t i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU; /* запретить все внешние IRQ */
        NVIC->ICPR[i] = 0xFFFFFFFFU; /* сбросить pending — не унаследовать флаг */
    }

    SysTick->CTRL = 0U;

    SCB_CleanInvalidateDCache();
    SCB_InvalidateICache();

    __DSB();
    __ISB();
}

static void jump_to_image(const struct boot_rsp *p_rsp)
{
    uintptr_t flash_base;
    if (flash_device_base(p_rsp->br_flash_dev_id, &flash_base) != 0)
    {
        return;
    }

    const struct arm_vector_table *p_vt =
        (const struct arm_vector_table *) (flash_base + p_rsp->br_image_off +
                                           p_rsp->br_hdr->ih_hdr_size);

    cleanup_before_jump();

    /* Образ сам переставит VTOR в своём Reset_Handler/SystemInit() — но до
     * этого момента (первые же инструкции после прыжка) он уже должен быть
     * валиден, на случай если что-то прервёт выполнение раньше. */
    SCB->VTOR = (uint32_t) p_vt;

    /* Намеренно НЕ __disable_irq()/PRIMASK здесь — см. cleanup_before_jump(). */
    __set_CONTROL(0U);
    __set_MSP(p_vt->msp);
    __ISB();
    ((void (*)(void)) p_vt->reset)();
}

void boot_select_and_jump(void)
{
    struct boot_rsp rsp;
    fih_ret fih_rc = boot_go(&rsp);

    if (!FIH_EQ(fih_rc, FIH_SUCCESS))
    {
        return; /* нет валидного образа — main.c продолжит ping/pong-цикл */
    }

    jump_to_image(&rsp); /* при успехе не возвращается */
}
