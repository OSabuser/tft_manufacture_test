/**
 * @file  boot_select.c
 * @brief bootutil boot_go() + прыжок в выбранный образ.
 *
 * Референс — sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/boot.c::do_boot():
 * та же последовательность (flash_device_base → вычислить адрес vector table →
 * __set_MSP → __ISB → прыжок на Reset_Handler), CMSIS-интринсики, без ассемблера.
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

static void jump_to_image(const struct boot_rsp *p_rsp)
{
    uintptr_t flash_base;
    if (flash_device_base(p_rsp->br_flash_dev_id, &flash_base) != 0)
    {
        return;
    }

    const struct arm_vector_table *p_vt = (const struct arm_vector_table *) (flash_base +
                                                                             p_rsp->br_image_off +
                                                                             p_rsp->br_hdr->ih_hdr_size);

    /* Намеренно НЕ __disable_irq() здесь (в отличие от более ранней версии).
     * bsp_delay() в целевом образе — busy-wait на счётчике, инкрементируемом
     * из SysTick_Handler (см. bsp/tick/src/tick.c); обычный Reset_Handler не
     * трогает PRIMASK, рассчитывая, что прерывания уже разрешены (как после
     * реального аппаратного ресета). Если замаскировать IRQ здесь и не
     * восстановить в целевом образе — SysTick не сработает ни разу, и любой
     * bsp_delay() внутри целевого образа зависнет навсегда. NXP-референс
     * (do_boot()) тоже не трогает PRIMASK — только __set_CONTROL/__set_MSP. */
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
