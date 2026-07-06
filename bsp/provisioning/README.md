# bsp_provisioning — Provisioning / OCOTP UID

Компонент читает уникальный 64-битный идентификатор чипа из OCOTP shadow registers.
Используется в `firmware_test` для команды `get_uid` протокола v2.

---

## Аппаратура

| Периферия | Назначение             |
| --------- | ---------------------- |
| OCOTP     | On-Chip OTP Controller |

Shadow registers загружаются из eFuse-массива при каждом сбросе (MIMXRT1052RM §46.3.1).
Чтение немедленное через memory-mapped регистры `OCOTP->CFG0` / `OCOTP->CFG1` —
инициализация контроллера, fuse programming и дополнительные задержки не нужны.

**UID-ячейки (MIMXRT1052RM Table 46-2):**

| Регистр    | Содержимое   |
| ---------- | ------------ |
| OCOTP_CFG0 | UID\[31:0\]  |
| OCOTP_CFG1 | UID\[63:32\] |

---

## API

```c
#include "bsp/provisioning.h"

#define BSP_PROV_UID_LEN 8U  /* байт */

bsp_status_t bsp_prov_read_uid(uint8_t *p_uid, size_t len);
```

`bsp_prov_read_uid()` — единственная публичная функция.

- Читает `OCOTP->CFG0` и `OCOTP->CFG1` напрямую через memory-mapped регистры
  (паттерн идентичен `fsl_silicon_id_soc.c` из NXP SDK).
- Заполняет `p_uid[0..7]` в little-endian порядке: `p_uid[0..3]` = CFG0, `p_uid[4..7]` = CFG1.
  Порядок байт совпадает с NXP-инструментами (MCUXpresso, blhost).
- Возвращает `BSP_ERR` если `p_uid == NULL` или `len < 8`.

---

## Использование

```c
#include "bsp/provisioning.h"
#include <stdio.h>

uint8_t uid[BSP_PROV_UID_LEN];
if (bsp_prov_read_uid(uid, sizeof(uid)) == BSP_OK)
{
    /* hex: "AABBCCDDEEFF0011" */
    for (size_t i = 0; i < BSP_PROV_UID_LEN; i++)
    {
        printf("%02X", uid[i]);
    }
}
```

---

## CMake

```cmake
target_link_libraries(firmware_test PRIVATE bsp_provisioning)
```

**Зависимости:**

| Зависимость  | Тип     | Описание                        |
| ------------ | ------- | ------------------------------- |
| `bsp_status` | PUBLIC  | `bsp_status_t` в публичном API  |
| `bsp_board`  | PRIVATE | Транзитивно: clock, SDK headers |

---

## Особенности

- Прямое чтение регистров не требует `OCOTP_Init()` — инициализация контроллера
  нужна только при записи fuse. Попытка вызвать `OCOTP_Init()` перед чтением
  приводит к зависанию (контроллер занят после USB CDC init).
- Компонент не имеет host-stub: защищён `if(BUILD_TESTS_HOST) return()` в CMakeLists.
