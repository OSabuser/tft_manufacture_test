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

Прошивка исполняется XIP из того же Flash через FlexSPI AHB-интерфейс.
CPU непрерывно читает инструкции через AHB, который маршрутизируется по
LUT-слоту 0. Любая IP-команда FlexSPI прерывает AHB-путь на время
выполнения команды. Если в момент IP-команды CPU попытается фетчить
инструкцию из Flash — HardFault.

### Решение

Все функции, обращающиеся к регистрам FlexSPI (IPCMD, IPCR0/1, RFDR, TFDR,
LUT, INTR, …), размещены в **ITCM** через `AT_QUICKACCESS_SECTION_CODE`.
Инструкции этих функций загружаются в ITCM при старте и CPU не обращается
к Flash во время их выполнения.

AHB prefetch отключается (`AHBCR.PREFETCHEN = 0`) перед каждой серией
IP-транзакций и восстанавливается после (`qspi_ahb_disable` / `qspi_ahb_enable`).
Это дополнительно предотвращает спекулятивные AHB-чтения Flash во время
IP-команд.

---

## Стратегия адресации W25Q256/512

### Почему не Enter 4-Byte Mode (0xB7)

FDCB задаёт XIP слот 0 с 24-bit адресацией для **всех** чипов. Если
переключить чип в 4-byte mode командой 0xB7, следующий AHB-фетч
инструкции из Flash пойдёт через слот 0 с 24-bit адресом, а чип теперь
ждёт 32-bit — **HardFault**.

### Dedicated 4-byte address opcodes

W25Q256/512 имеют отдельный набор opcodes, принимающих 32-bit адрес
**независимо от текущего режима адресации чипа**:

| Операция          | 3-byte opcode (W25Q64/128) | 4-byte opcode (W25Q256/512) |
|-------------------|----------------------------|-----------------------------|
| Sector Erase 4KB  | `0x20`                     | `0x21`                      |
| Block Erase 32KB  | `0x52`                     | `0x5C`                      |
| Block Erase 64KB  | `0xD8`                     | `0xDC`                      |
| Quad Page Program | `0x32`                     | `0x34`                      |
| Quad Output Read  | `0x6B`                     | `0x6C`                      |

Слот 0 (XIP) **не изменяется**. Чип остаётся в 24-bit режиме. XIP работает
непрерывно на всех чипах.

---

## LUT-слоты

| Слот | Константа      | Команда                    | Зависит от чипа |
|------|----------------|----------------------------|-----------------|
| 0    | (XIP, FDCB)    | Quad Read                  | Нет (не трогаем)|
| 1    | LSEQ_READ_SR1  | Read SR1 (0x05)            | Нет             |
| 2    | LSEQ_WR_EN     | Write Enable (0x06)        | Нет             |
| 3    | LSEQ_ERASE_4K  | Sector Erase (0x20/0x21)   | Да              |
| 4    | LSEQ_PP_QUAD   | Quad Page Prog (0x32/0x34) | Да              |
| 5    | LSEQ_JEDEC     | Read JEDEC (0x9F)          | Нет             |
| 6    | LSEQ_READ_SR2  | Read SR2 (0x35)            | Нет             |
| 7    | LSEQ_WR_SR2    | Write SR2 (0x31)           | Нет             |
| 8    | LSEQ_READ_SR3  | Read SR3 (0x15)            | Нет             |
| 9    | LSEQ_ERASE_32K | Block Erase 32KB (0x52/0x5C)| Да             |
| 10   | LSEQ_ERASE_64K | Block Erase 64KB (0xD8/0xDC)| Да             |
| 11   | LSEQ_IP_READ   | Quad Out Read (0x6B/0x6C)  | Да              |

---

## Публичный API

```c
/* Инициализация — первый вызов */
bsp_status_t bsp_qspi_init(void);

/* Чтение JEDEC ID */
bsp_status_t bsp_qspi_read_jedec_id(bsp_qspi_jedec_t *p_jedec);

/* Стирание */
bsp_status_t bsp_qspi_erase_sector(uint32_t addr);     /* 4KB,  ~45 мс  */
bsp_status_t bsp_qspi_erase_block_32k(uint32_t addr);  /* 32KB, ~120 мс */
bsp_status_t bsp_qspi_erase_block_64k(uint32_t addr);  /* 64KB, ~150 мс */

/* Запись одной страницы */
bsp_status_t bsp_qspi_write_page(uint32_t addr, const uint8_t *p_data); /* ~3 мс */

/* Чтение произвольного числа байт (через IP-команду, не AHB) */
bsp_status_t bsp_qspi_read(uint32_t addr, uint8_t *p_buf, size_t size);

/* Размер Flash — доступен после init */
uint32_t bsp_qspi_flash_size(void);
```

---

## Выбор операции стирания для массовой очистки

| Объём очистки    | Рекомендуемая операция | Примерное время |
|------------------|------------------------|-----------------|
| < 32 KB          | `erase_sector` (4KB)   | пропорционально |
| 32 KB — 1 MB     | `erase_block_32k`      | 120 мс / 32KB   |
| > 1 MB           | `erase_block_64k`      | 150 мс / 64KB   |

Пример: очистка 4 MB через 64KB блоки = 64 операции × 150 мс ≈ **9.6 с**
против 1024 × 45 мс ≈ **46 с** через 4KB секторы.

---

## Порядок вызова

```c
/* 1. Инициализация (один раз) */
if (bsp_qspi_init() != BSP_OK) { /* обработка ошибки */ }

/* 2. Операции  */
bsp_qspi_erase_sector(addr);    /* блокирует ~45 мс */


bsp_qspi_write_page(addr, buf); /* блокирует ~3 мс */

bsp_qspi_read(addr, buf, 256);  /* быстро */
```

---

## CMake

```cmake
# bsp/qspi/CMakeLists.txt
target_link_libraries(bsp_qspi
    PUBLIC  bsp_status
    PRIVATE bsp_board sdk_flexspi
)
```

Потребители (например, `firmware_test`) линкуют `bsp_qspi`:

```cmake
target_link_libraries(firmware_test PRIVATE ... bsp_qspi)
```

---

## Известные ограничения

- `bsp_qspi_write_page()` записывает строго одну страницу (256 байт).
  Запись через границу страницы не поддерживается — вызывающий код
  обязан выровнять адрес.
- Нет timeout в `qspi_wait_not_busy()`. Зависание из-за дефектного чипа
  потребует watchdog reset. Для диагностической прошивки это приемлемо.
- Chip Erase (0xC7) не реализован — слишком деструктивно для XIP-прошивки.
  Используй `erase_block_64k` в цикле по всему адресному пространству.