#include "board.h"
#include "bsp/led.h"

int main(void)
{
    board_hw_init();
    led_init();

    led_on(LED_HEARTBEAT);
    led_off(LED_APP);
    while (1)
    {
    }
}
