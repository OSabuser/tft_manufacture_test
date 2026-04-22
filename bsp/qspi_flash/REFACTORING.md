Нашёл и закрыл основную причину HardFault.

Что было не так

Ваша CodeQuickAccess/.ram_function секция не копировалась в ITCM при старте, потому что для startup_MIMXRT1052.S не были заданы макросы __STARTUP_INITIALIZE_RAMFUNCTION (и __STARTUP_CLEAR_BSS).
SysTick включался до bsp_qspi_init(), а в драйвере не было маскирования IRQ на время FlexSPI IP-операций, что опасно при XIP с той же флешки.
Что я изменил

Добавил startup-дефайны в firmware/test/CMakeLists.txt (line 20):

- `__STARTUP_INITIALIZE_RAMFUNCTION`
- `__STARTUP_CLEAR_BSS`
- 
Переставил порядок инициализации в firmware/test/src/main.c (line 38):
bsp_qspi_init() теперь до bsp_tick_init().

Усилил XIP-safe поведение в bsp/qspi_flash/src/qspi_flash.c (line 205):
добавил qspi_irq_lock()/qspi_irq_unlock();

обернул критичные операции (init/read/write/erase) под IRQ lock;
публичные QSPI API размещены в CodeQuickAccess (ITCM).


Нашёл вероятный корень: в драйвере жёстко зашит watermark = 8 bytes, а в железе он может быть другим, из-за чего TX/RX FIFO-петли начинают висеть. Сейчас внесу фикс: динамический watermark из регистров + ожидание IPCMDDONE как в fsl_flexspi.

Да, это мой хвост после рефактора watermark-констант. Сейчас поправлю qspi_write_fifo на динамический watermark и сразу проверю сборку, чтобы не оставить таких разрывов.