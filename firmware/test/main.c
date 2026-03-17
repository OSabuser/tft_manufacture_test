#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"

int main(void)
{

    board_hw_init();
    bsp_led_init();
    bsp_tick_init();
    const uint16_t DELAY_MS = 50;
    while (1)
    {
        bsp_led_on(LED_HEARTBEAT);
        bsp_delay(DELAY_MS);
        bsp_led_off(LED_HEARTBEAT);
        bsp_delay(DELAY_MS);
    }
}
