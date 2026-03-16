#include "board.h"

#include "clock_config.h"
#include "pin_mux.h"

void board_hw_init(void)
{
    BOARD_InitPins();
    BOARD_BootClockRUN();
}
