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
 * Найдено на реальном железе (Фаза 3): Slot A с уже валидным подтверждённым
 * образом не загружался, когда прыжок происходил сразу после
 * bsp_usb_cdc_init() (USB ещё в процессе enumeration — прерывания частые), но
 * загружался, когда прыжок происходил позже (после цикла ожидания SD — USB
 * уже в устоявшемся состоянии). Причина: bootloader (в отличие от Фазы 1/2,
 * где до прыжка включался только bsp_qspi_init() — без единого постоянно
 * включённого NVIC IRQ) теперь включает USB CDC и, при вставленной SD,
 * USDHC — оба взводят свои NVIC IRQ. jump_to_image() не переключал VTOR —
 * прерывание, сработавшее в узком окне между прыжком и тем, как целевой
 * образ успеет настроить свою таблицу векторов в Reset_Handler/SystemInit(),
 * уходило по ещё активной (bootloader'овской) таблице векторов с чужим
 * стеком/контекстом.
 *
 * Портировано по мотивам cleanup()/SBL_DisablePeripherals() в референсном
 * sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/boot.c::do_boot() — не
 * скопировано напрямую (SBL_DisablePeripherals — extern, платформенно-
 * специфичная функция, не вендоренная в нашем дереве); здесь общий,
 * не завязанный на конкретную периферию эквивалент через NVIC/SysTick.
 *
 * НЕ трогает PRIMASK (в отличие от более ранней, откаченной в Фазе 2
 * версии) — SysTick_Handler целевого образа должен сработать после того как
 * образ сам вызовет bsp_tick_init(), а PRIMASK обычный Reset_Handler не
 * восстанавливает (см. bsp_delay()-зависание, найденное в Фазе 2).
 * Отключение конкретных источников (NVIC ICER/ICPR, SysTick->CTRL) — не
 * то же самое, что глобальная маскировка: раз выключенный SysTick просто не
 * тикает, пока образ не включит его сам, и не блокирует его же будущий
 * запуск.
 */
static void cleanup_before_jump(void)
{
    for (uint32_t i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU; /* запретить все внешние IRQ */
        NVIC->ICPR[i] = 0xFFFFFFFFU; /* сбросить pending — не унаследовать флаг */
    }

    SysTick->CTRL = 0U; /* SysTick — не в NVIC->ICER, отдельный системный таймер */

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
