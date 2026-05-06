# bsp_qspi_flash — QSPI Flash W25Q64/128/256/512

> Расположение: `bsp/qspi_flash/`
> Публичный заголовок: `bsp/qspi_flash/include/bsp/qspi_flash.h`
> Реализация: `bsp/qspi_flash/src/qspi_flash.c`

---

## Поддерживаемое железо

| Чип     | JEDEC mfr | JEDEC cap | Размер | LUT-таблица |
|---------|-----------|-----------|--------|-------------|
| W25Q64  | 0xEF      | 0x17      |  8 MB  | `K_LUT_3B`  |
| W25Q128 | 0xEF      | 0x18      | 16 MB  | `K_LUT_3B`  |
| W25Q256 | 0xEF      | 0x19      | 32 MB  | `K_LUT_4B`  |
| W25Q512 | 0xEF      | 0x20      | 64 MB  | `K_LUT_4B`  |

Интерфейс MCU: **FlexSPI1, порт A1** (`FLEXSPI_PortA1`).

---

## XIP-безопасность

### Проблема

Прошивка исполняется XIP из Flash через FlexSPI AHB-интерфейс.
CPU непрерывно фетчит инструкции из Flash по AHB — через LUT-слот 0.
Любая IP-команда FlexSPI блокирует AHB-путь на время выполнения.
Если в этот момент CPU попытается фетчить инструкцию из Flash — **HardFault**.

### Решение: ITCM + IRQ lock

Все функции, обращающиеся к регистрам FlexSPI, размещены в **ITCM**
(`0x00000000`) через `AT_QUICKACCESS_SECTION_CODE`. ITCM подключён к CPU
по выделенной шине (не AHB), поэтому фетч инструкций из ITCM не конкурирует
с IP-командами FlexSPI.

Дополнительно, каждая публичная операция выполняется под **IRQ lock**
(`qspi_irq_lock` / `qspi_irq_unlock` с `__get_PRIMASK()` + `DSB` + `ISB`):
это гарантирует, что прерывание не застанет FlexSPI в середине IP-транзакции.
IRQ unlock восстанавливает предыдущий `PRIMASK`, не включает IRQ безусловно —
вызов из уже заблокированного контекста корректен.

AHB prefetch отключается (`AHBCR.PREFETCHEN = 0`) перед серией IP-транзакций
и восстанавливается после.

### Обязательный дефайн в CMakeLists потребителя

```cmake
target_compile_definitions(firmware_test PRIVATE
    __STARTUP_INITIALIZE_RAMFUNCTION   # ← обязательно
    __STARTUP_CLEAR_BSS)
```

Без `__STARTUP_INITIALIZE_RAMFUNCTION` startup-файл NXP SDK не копирует
`CodeQuickAccess` секцию из Flash в ITCM. В ITCM остаются нули. Первый же
вызов любой ITCM-функции вызывает **HardFault**.

---

## Стратегия адресации W25Q256/512

### Почему не Enter 4-Byte Mode (0xB7)

FDCB фиксирует XIP-слот 0 в режиме 24-bit адресации на всех чипах.
Переключение чипа командой 0xB7 сломало бы XIP — AHB продолжал бы
посылать 24-bit адреса, чип ждал бы 32-bit → **HardFault**.

### Dedicated 4-byte address opcodes

W25Q256/512 принимают 32-bit адрес через отдельный набор opcodes — без
изменения режима адресации чипа:

| Операция          | W25Q64/128 (3-byte) | W25Q256/512 (4-byte) |
|-------------------|---------------------|----------------------|
| Sector Erase 4KB  | `0x20`              | `0x21`               |
| Block Erase 32KB  | `0x52`              | `0x5C`               |
| Block Erase 64KB  | `0xD8`              | `0xDC`               |
| Quad Page Program | `0x32`              | `0x34`               |
| IP Quad Out Read  | `0x6B`              | `0x6C`               |

Слот 0 (XIP) **не изменяется**. XIP работает непрерывно на всех чипах.

---

## LUT-слоты

| Слот | Константа      | Команда                     | Зависит от чипа |
|------|----------------|-----------------------------|-----------------|
| 0    | (XIP, FDCB)    | Quad Read                   | Нет (не трогаем)|
| 1    | LSEQ_READ_SR1  | Read SR1 (0x05)             | Нет             |
| 2    | LSEQ_WR_EN     | Write Enable (0x06)         | Нет             |
| 3    | LSEQ_ERASE_4K  | Sector Erase (0x20/0x21)    | Да              |
| 4    | LSEQ_PP_QUAD   | Quad Page Prog (0x32/0x34)  | Да              |
| 5    | LSEQ_JEDEC     | Read JEDEC (0x9F)           | Нет             |
| 6    | LSEQ_READ_SR2  | Read SR2 (0x35)             | Нет             |
| 7    | LSEQ_WR_SR2    | Write SR2 (0x31)            | Нет             |
| 8    | LSEQ_READ_SR3  | Read SR3 (0x15)             | Нет             |
| 9    | LSEQ_ERASE_32K | Block Erase 32KB (0x52/0x5C)| Да              |
| 10   | LSEQ_ERASE_64K | Block Erase 64KB (0xD8/0xDC)| Да              |
| 11   | LSEQ_IP_READ   | Quad Out Read (0x6B/0x6C)   | Да              |

Слоты 1–11 обновляются в `bsp_qspi_init()` под конкретный чип.
Слот 0 никогда не изменяется BSP-кодом.

---

## Watermark FIFO

Размер watermark-юнита читается из регистров `IPRXFCR.RXWMRK` и
`IPTXFCR.TXWMRK` в рантайме — не зашит константой. Это гарантирует
корректную работу если FDCB или SDK изменили настройки watermark по
умолчанию.

---

## Публичный API

Все публичные функции размещены в ITCM (`AT_QUICKACCESS_SECTION_CODE`) и
выполняются под IRQ lock.

```c
/* Инициализация — вызвать до bsp_tick_init() и любой другой функции модуля */
bsp_status_t bsp_qspi_init(void);

/* Чтение JEDEC ID (0x9F) */
bsp_status_t bsp_qspi_read_jedec_id(bsp_qspi_jedec_t *p_jedec);

/* Стирание */
bsp_status_t bsp_qspi_erase_sector(uint32_t addr);     /* 4KB,  ~45 мс  */
bsp_status_t bsp_qspi_erase_block_32k(uint32_t addr);  /* 32KB, ~120 мс */
bsp_status_t bsp_qspi_erase_block_64k(uint32_t addr);  /* 64KB, ~150 мс */

/* Запись одной страницы (256 байт) */
bsp_status_t bsp_qspi_write_page(uint32_t addr, const uint8_t *p_data); /* ~3 мс */

/* Чтение через IP-команду (не AHB/XIP) */
bsp_status_t bsp_qspi_read(uint32_t addr, uint8_t *p_buf, size_t size);

/* Размер Flash — доступен после bsp_qspi_init() */
uint32_t bsp_qspi_flash_size(void);
```

Все функции возвращают `BSP_OK` при успехе или `BSP_ERR_HW` при ошибке
FlexSPI / неверном аргументе. `BSP_ERR_HW` должен присутствовать в
`bsp/common/include/bsp/status.h`.

---

## Порядок инициализации

`bsp_qspi_init()` должна вызываться **до** `bsp_tick_init()`:

```c
/* main.c */
board_hw_init();
bsp_qspi_init();    /* ← сначала QSPI, до SysTick */
bsp_tick_init();    /* ← потом SysTick */
bsp_usb_cdc_init();
```

Причина: `bsp_qspi_init()` и все ITCM-функции выполняются под полным
IRQ lock. Если SysTick уже запущен и прерывание сработает в момент
IP-команды — возможен AHB stall. Порядок инициализации устраняет эту
гонку при первом запуске.

---

## Выбор операции стирания

| Объём очистки | Рекомендация           | Время           |
|---------------|------------------------|-----------------|
| < 32 KB       | `erase_sector` (4KB)   | пропорционально |
| 32 KB — 1 MB  | `erase_block_32k`      | ~120 мс / 32KB  |
| > 1 MB        | `erase_block_64k`      | ~150 мс / 64KB  |

Пример: 4 MB через 64KB = 64 × 150 мс ≈ **9.6 с**
против 1024 × 45 мс ≈ **46 с** через 4KB.

---

## CMake

```cmake
# bsp/qspi_flash/CMakeLists.txt
target_link_libraries(bsp_qspi_flash
    PUBLIC  bsp_status
    PRIVATE bsp_board sdk_flexspi
)
```

Потребитель (`firmware_test`):

```cmake
target_link_libraries(firmware_test PRIVATE
    bsp_qspi_flash
    ...
)

target_compile_definitions(firmware_test PRIVATE
    __STARTUP_INITIALIZE_RAMFUNCTION   # обязательно для ITCM-функций
    __STARTUP_CLEAR_BSS
)
```

---

## Известные ограничения

- `bsp_qspi_write_page()` — строго одна страница (256 байт). Адрес обязан
  быть выровнен на `BSP_QSPI_PAGE_SIZE`. Запись через границу страницы не
  поддерживается.
- Нет timeout в `qspi_wait_not_busy()`. Зависание из-за дефектного чипа
  потребует watchdog reset. Для диагностической прошивки это приемлемо.
- Chip Erase (0xC7) не реализован — слишком деструктивно при XIP-исполнении.
