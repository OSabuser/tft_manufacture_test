/**
 * @file  bsp_mqs_amp.c
 * @brief BSP: управление усилителем звука LM4875M через FlexPWM4 SM0 PWM_A.
 *
 * Схема:
 *   GPIO_AD_B1_08 (PWM4_A, SM0) → R56 (1K) → LM358 +IN1 → LM4875M DC_VOL
 *   DC_VOL управляет коэффициентом усиления LM4875M.
 *   HP_SEN LM4875M подтянут к GND — включается автоматически при наличии
 *   аудио-сигнала на входе (MQS_RIGHT через RC-фильтр → SOUND_OUT).
 *
 * Управление громкостью:
 *   PWM4 SM0 PWM_A, частота 16 кГц, центрально-симметричный режим.
 *   duty 0%  → DC_VOL ≈ 0 В → усиление минимально (тишина).
 *   duty 50% → DC_VOL ≈ 2.5 В → номинальная громкость.
 *   duty 100%→ DC_VOL ≈ 5 В → максимальное усиление.
 *
 * Fault-вход PWM4:
 *   XBARA1: kXBARA1_InputLogicHigh → kXBARA1_OutputFlexpwm4Fault0.
 *   Fault сконфигурирован как "no fault" — PWM работает без аппаратного
 *   отключения. Это повторяет конфигурацию из оригинального timers.c.
 *
 * Тактирование:
 *   IPG clock = AHB/4 = 600/4 = 150 МГц.
 *   PWM prescaler = /16 → PWM clock = 9.375 МГц.
 *   Fpwm = 16000 Гц (центрально-симметричный режим).
 */

#include "bsp/mqs.h"
#include "clock_config.h"
#include "fsl_pwm.h"
#include "fsl_xbara.h"

/* --------------------------------------------------------------------------
 * Конфигурация
 * ----------------------------------------------------------------------- */

#define AMP_PWM_BASE      PWM4
#define AMP_PWM_SUBMODULE kPWM_Module_0
#define AMP_PWM_CHANNEL   kPWM_PwmA
#define AMP_PWM_MODE      kPWM_SignedCenterAligned
#define AMP_PWM_FREQ_HZ   (12000U)

/** Номинальный duty cycle при bsp_mqs_amp_init() — 50%. */
#define AMP_DEFAULT_DUTY (50U)

/** IPG clock = 150 МГц (AHB/4). */
#define AMP_PWM_SRC_CLK_HZ (BOARD_BOOTCLOCKRUN_IPG_CLK_ROOT)

/* --------------------------------------------------------------------------
 * Публичный API
 * ----------------------------------------------------------------------- */

bsp_status_t bsp_mqs_amp_init(void)
{
    /* --- XBARA1: подать логическую единицу на Fault0 PWM4.
     *  Это предотвращает аппаратное отключение PWM при старте. --- */
    XBARA_Init(XBARA1);
    XBARA_SetSignalsConnection(XBARA1, kXBARA1_InputLogicHigh, kXBARA1_OutputFlexpwm4Fault0);

    /* --- PWM4 SM0: базовая конфигурация --- */
    pwm_config_t pwm_cfg;
    PWM_GetDefaultConfig(&pwm_cfg);
    pwm_cfg.clockSource           = kPWM_BusClock;
    pwm_cfg.prescale              = kPWM_Prescale_Divide_16;
    pwm_cfg.pairOperation         = kPWM_Independent;
    pwm_cfg.initializationControl = kPWM_Initialize_LocalSync;
    pwm_cfg.reloadLogic           = kPWM_ReloadImmediate;
    pwm_cfg.reloadSelect          = kPWM_LocalReload;
    pwm_cfg.reloadFrequency       = kPWM_LoadEveryOportunity;
    pwm_cfg.forceTrigger          = kPWM_Force_Local;

    if (PWM_Init(AMP_PWM_BASE, AMP_PWM_SUBMODULE, &pwm_cfg) != kStatus_Success)
    {
        return BSP_ERR_HW;
    }

    /* --- Fault-фильтр и fault-конфигурация (no fault active) --- */
    const pwm_fault_input_filter_param_t FAULT_FILTER = {
        .faultFilterPeriod  = 1U,
        .faultFilterCount   = 3U,
        .faultGlitchStretch = false,
    };
    PWM_SetupFaultInputFilter(AMP_PWM_BASE, &FAULT_FILTER);

    const pwm_fault_param_t FAULT_CFG = {
        .faultClearingMode       = kPWM_Automatic,
        .faultLevel              = false,
        .enableCombinationalPath = false,
        .recoverMode             = kPWM_NoRecovery,
    };
    PWM_SetupFaults(AMP_PWM_BASE, kPWM_Fault_0, &FAULT_CFG);
    PWM_SetupFaults(AMP_PWM_BASE, kPWM_Fault_1, &FAULT_CFG);
    PWM_SetupFaults(AMP_PWM_BASE, kPWM_Fault_2, &FAULT_CFG);
    PWM_SetupFaults(AMP_PWM_BASE, kPWM_Fault_3, &FAULT_CFG);

    /* --- ForceSignal: использовать нормальный PWM-сигнал --- */
    PWM_SetupForceSignal(AMP_PWM_BASE, AMP_PWM_SUBMODULE, AMP_PWM_CHANNEL, kPWM_UsePwm);

    /* --- PWM-сигнал: 16 кГц, центрально-симметричный, duty 50% --- */
    const pwm_signal_param_t PWM_SIGNAL = {
        .pwmChannel       = AMP_PWM_CHANNEL,
        .dutyCyclePercent = AMP_DEFAULT_DUTY,
        .level            = kPWM_HighTrue,
        .faultState       = kPWM_PwmFaultState0,
        .deadtimeValue    = 0U,
        .pwmchannelenable = true,
    };

    if (PWM_SetupPwm(AMP_PWM_BASE, AMP_PWM_SUBMODULE, &PWM_SIGNAL, 1U, AMP_PWM_MODE,
                     AMP_PWM_FREQ_HZ, (uint32_t) AMP_PWM_SRC_CLK_HZ) != kStatus_Success)
    {
        return BSP_ERR_HW;
    }

    /* --- Применить и запустить --- */
    PWM_SetPwmLdok(AMP_PWM_BASE, (uint8_t) kPWM_Control_Module_0, true);
    PWM_StartTimer(AMP_PWM_BASE, (uint8_t) kPWM_Control_Module_0);

    return BSP_OK;
}

void bsp_mqs_amp_set_volume(uint8_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }

    PWM_UpdatePwmDutycycle(AMP_PWM_BASE, AMP_PWM_SUBMODULE, AMP_PWM_CHANNEL, AMP_PWM_MODE, percent);
    PWM_SetPwmLdok(AMP_PWM_BASE, (uint8_t) kPWM_Control_Module_0, true);
}

void bsp_mqs_amp_deinit(void)
{
    /* Установить duty 0% перед остановкой — плавное выключение. */
    PWM_UpdatePwmDutycycle(AMP_PWM_BASE, AMP_PWM_SUBMODULE, AMP_PWM_CHANNEL, AMP_PWM_MODE, 0U);
    PWM_SetPwmLdok(AMP_PWM_BASE, (uint8_t) kPWM_Control_Module_0, true);

    PWM_StopTimer(AMP_PWM_BASE, (uint8_t) kPWM_Control_Module_0);
    PWM_Deinit(AMP_PWM_BASE, AMP_PWM_SUBMODULE);
}