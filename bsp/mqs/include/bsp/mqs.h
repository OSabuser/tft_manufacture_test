/**
 * @file  bsp/mqs.h
 * @brief BSP: Medium Quality Sound (MQS) — SAI1 + eDMA + MQS.
 *
 * Слой абстракции над SAI1/eDMA/MQS для монофонического аудио-выхода.
 * Физически на плате выведен один канал (MQS_RIGHT, GPIO_AD_B0_04);
 * SAI1 требует стерео-буфер — оба канала всегда идентичны.
 *
 * Режимы использования:
 *   - firmware_test: bsp_mqs_play_blocking() — синхронная подача
 *                    статического буфера, polling-флаг завершения.
 *   - tft_app:       bsp_mqs_play() + колбэк — асинхронная потоковая
 *                    подача буферов из задачи FreeRTOS.
 *
 * Ограничения:
 *   - Буфер должен быть в некэшируемой памяти (AT_NONCACHEABLE_SECTION_ALIGN)
 *     или кэш должен быть явно вычищен перед вызовом bsp_mqs_play().
 *   - Одновременно может воспроизводиться только один буфер.
 *   - bsp_mqs_init() вызывается однократно; повторный вызов без
 *     bsp_mqs_deinit() возвращает BSP_OK без переинициализации.
 */

#ifndef BSP_MQS_H
#define BSP_MQS_H

#include "bsp/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * Параметры аудио-потока
 * ----------------------------------------------------------------------- */

/** Частота дискретизации, Гц. Небольшое отклонение (~0.5%) из-за
 *  источника SAI1_CLK_ROOT (System PLL PFD2, не Audio PLL). */
#define BSP_MQS_SAMPLE_RATE_HZ (44100U)

/** Разрядность PCM. MQS поддерживает только 16 бит. */
#define BSP_MQS_BIT_WIDTH (16U)

/** Количество каналов в буфере. SAI1+MQS требует стерео; правый == левый. */
#define BSP_MQS_CHANNELS (2U)

/** Байт на один моно-сэмпл (16 бит → 2 байта). */
#define BSP_MQS_BYTES_PER_SAMPLE (BSP_MQS_BIT_WIDTH / 8U)

/** Байт на один стерео-фрейм (L + R). */
#define BSP_MQS_BYTES_PER_FRAME (BSP_MQS_BYTES_PER_SAMPLE * BSP_MQS_CHANNELS)

    /* --------------------------------------------------------------------------
 * Колбэк завершения (для tft_app / потокового режима)
 * ----------------------------------------------------------------------- */

    /**
 * @brief Колбэк завершения DMA-передачи одного буфера.
 *
 * Вызывается из контекста ISR. Запрещено: блокировки, HAL-функции
 * без суффикса _FromISR, длительные вычисления.
 *
 * @param p_user  Пользовательский указатель, переданный в bsp_mqs_play().
 */
    typedef void (*bsp_mqs_done_cb_t)(void *p_user);

    /* --------------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

    /**
 * @brief Инициализация MQS-подсистемы: SAI1, eDMA, DMAMUX, MQS.
 *
 * Включает тактирование SAI1 (kCLOCK_Sai1), настраивает SAI1 в режиме
 * TX Master, 16 бит, стерео, 44100 Гц, инициализирует eDMA канал 0
 * (DMAMUX source kDmaRequestMuxSai1Tx) и MQS-модуль.
 *
 * Пин GPIO_AD_B0_04 (MQS_RIGHT) уже замультиплексирован в BOARD_InitPins().
 * MQS oversample (×32) уже выставлен в BOARD_BootClockRUN().
 *
 * @return BSP_OK      успех
 * @return BSP_ERR_HW  eDMA или SAI вернул ошибку при настройке формата
 */
    bsp_status_t bsp_mqs_init(void);

    /**
 * @brief Деинициализация: остановить DMA, сбросить SAI1 и MQS.
 *
 * Безопасно вызывать даже если воспроизведение уже завершилось.
 * После вызова модуль требует повторного bsp_mqs_init().
 */
    void bsp_mqs_deinit(void);

    /**
 * @brief Запустить асинхронное воспроизведение буфера через eDMA.
 *
 * Буфер должен содержать стерео PCM16 interleaved (L0, R0, L1, R1, …).
 * Указатель @p p_buf обязан оставаться валидным до срабатывания колбэка @p cb.
 *
 * @param p_buf    Указатель на стерео PCM16 буфер (некэшируемая память).
 * @param n_frames Число стерео-фреймов (не байт, не сэмплов).
 * @param cb       Колбэк завершения; NULL — без уведомления.
 * @param p_user   Аргумент для колбэка; NULL допустим.
 *
 * @return BSP_OK          передача запущена
 * @return BSP_ERR_BUSY    предыдущая передача ещё не завершена
 * @return BSP_ERR_INVALID p_buf == NULL или n_frames == 0
 * @return BSP_ERR_HW      SAI_TransferSendEDMA вернул ошибку
 */
    bsp_status_t bsp_mqs_play(const int16_t *p_buf, size_t n_frames, bsp_mqs_done_cb_t cb,
                              void *p_user);

    /**
 * @brief Синхронное воспроизведение: запустить и дождаться завершения.
 *
 * Вызывает bsp_mqs_play() и крутится в polling-цикле до сброса флага
 * завершения. Предназначен для bare-metal (firmware_test).
 * Не вызывать из ISR или FreeRTOS-задачи с высоким приоритетом.
 *
 * @param p_buf    Указатель на стерео PCM16 буфер (некэшируемая память).
 * @param n_frames Число стерео-фреймов.
 *
 * @return BSP_OK          воспроизведение завершено
 * @return BSP_ERR_BUSY    предыдущая передача ещё не завершена
 * @return BSP_ERR_INVALID p_buf == NULL или n_frames == 0
 * @return BSP_ERR_HW      SAI_TransferSendEDMA вернул ошибку
 */
    bsp_status_t bsp_mqs_play_blocking(const int16_t *p_buf, size_t n_frames);

    /**
 * @brief Немедленно прервать воспроизведение.
 *
 * Останавливает текущую DMA-передачу. Колбэк после stop НЕ вызывается.
 * Безопасно вызывать если передача уже завершилась.
 */
    void bsp_mqs_stop(void);

    /**
 * @brief Проверить, активна ли DMA-передача.
 *
 * @return true   передача идёт
 * @return false  передача завершена или не была запущена
 */
    bool bsp_mqs_is_busy(void);

    /* --------------------------------------------------------------------------
 * Усилитель (LM4875M, управление через PWM4 SM0 PWM_A)
 * ----------------------------------------------------------------------- */

    /**
 * @brief Инициализация усилителя: PWM4 SM0, 16 кГц, duty 50%.
 *
 * Настраивает XBARA1 (fault disable), PWM4 submodule 0 channel A.
 * Вызывать до bsp_mqs_play() — без ШИМ на VOLUME усиление равно нулю.
 *
 * @return BSP_OK      успех
 * @return BSP_ERR_HW  PWM_Init или PWM_SetupPwm вернул ошибку
 */
    bsp_status_t bsp_mqs_amp_init(void);

    /**
 * @brief Установить уровень громкости (duty cycle PWM).
 *
 * @param percent  Громкость 0–100%. 0 = тишина, 50 = номинал, 100 = макс.
 */
    void bsp_mqs_amp_set_volume(uint8_t percent);

    /**
 * @brief Деинициализация усилителя: duty → 0%, стоп таймера.
 */
    void bsp_mqs_amp_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_MQS_H */