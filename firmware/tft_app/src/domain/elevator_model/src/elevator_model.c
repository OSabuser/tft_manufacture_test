#include "domain/elevator_model.h"

sul_result_t sul_default_state(void)
{
    return (sul_result_t){.pos = "--", .direction = SUL_DIR_NONE};
}
