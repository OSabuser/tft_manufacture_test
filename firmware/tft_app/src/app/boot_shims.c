/*
 * boot_shims.c — заглушки символов, которые дёргает общий с bootloader
 * flash_map_backend.c, но которые не имеют смысла в контексте app.
 *
 * flash_map_backend.c зовёт led_status_tick_install() из цикла поблочного
 * erase, чтобы двигать LED-анимацию прогресса в bootloader. В app окно
 * установки не открыто (LED-индикацией управляет прикладная логика), поэтому
 * здесь это no-op — ровно как у test_stub, но без утаскивания led_status.c
 * (и его зависимости от bare-metal bsp_tick, конфликтующего с FreeRTOS-SysTick).
 */

void led_status_tick_install(void)
{
    /* no-op: у app нет окна LED-установки */
}
