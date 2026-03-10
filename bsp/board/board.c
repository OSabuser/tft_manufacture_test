#include "board.h"
#include "clock_config.h"
#include "pin_mux.h"

void BOARD_Init(void) {
  BOARD_InitPins();
  BOARD_BootClockRUN();
}
