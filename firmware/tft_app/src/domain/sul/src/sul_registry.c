#include "domain/sul.h"

#include "domain/sul/nku_can.h"

#include <stddef.h>

static const sul_driver_t s_registry[] = {
    {
        .id     = SUL_PROTOCOL_NKU_CAN,
        .p_name = "НКУ-CAN",
        .decode = nku_can_decode,
    },
};

#define REGISTRY_COUNT (sizeof(s_registry) / sizeof(s_registry[0]))

const sul_driver_t *sul_registry_active(void)
{
    /* Фаза 1: единственная запись, хардкод. Фаза 3 выберет по настройкам. */
    return &s_registry[0];
}

const sul_driver_t *sul_registry_find(uint8_t id)
{
    for (size_t i = 0; i < REGISTRY_COUNT; i++)
    {
        if (s_registry[i].id == id)
        {
            return &s_registry[i];
        }
    }
    return NULL;
}
