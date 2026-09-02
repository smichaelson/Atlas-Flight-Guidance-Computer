/**
 * @file atlas_buzzer.c
 * @brief Opposite-phase TIM15 PWM drive for the Atlas differential piezo buzzer.
 *
 * Major functions:
 * - AtlasBuzzer_Init(): determines the timer clock and establishes a silent state.
 * - AtlasBuzzer_Start(): uses PWM1/PWM2 modes to prevent identical in-phase outputs.
 * - AtlasBuzzer_Beep(): schedules a tone without delaying flight-control code.
 * - AtlasBuzzer_Service(): handles HAL tick wrap safely when stopping a beep.
 */

#include "atlas_buzzer.h"

#include <string.h>

/**
 * @brief Determine the TIM15 kernel clock under the generated APB2 clock setup.
 * @return Timer clock in hertz.
 * @note The generated design uses the conventional x2 timer clock when APB prescaler > 1.
 */
static uint32_t atlas_buzzer_timer_clock_hz(void)
{
    RCC_ClkInitTypeDef clocks;
    uint32_t flash_latency;
    uint32_t timer_clock = HAL_RCC_GetPCLK2Freq();

    HAL_RCC_GetClockConfig(&clocks, &flash_latency);
    if (clocks.APB2CLKDivider != RCC_HCLK_DIV1)
    {
        timer_clock *= 2U;
    }
    return timer_clock;
}

/**
 * @brief Configure channel 1 as PWM1 and channel 2 as PWM2 at equal duty.
 * @param buzzer Initialized driver instance.
 * @param frequency_hz Requested output frequency.
 * @return ATLAS_OK or a typed range/HAL failure.
 */
static AtlasStatus atlas_buzzer_configure(AtlasBuzzer *buzzer,
                                          uint32_t frequency_hz)
{
    TIM_OC_InitTypeDef channel = {0};
    const uint32_t timer_tick_hz = buzzer->timer_clock_hz /
                                   (buzzer->timer->Init.Prescaler + 1U);
    uint32_t counts;

    if ((frequency_hz < ATLAS_BUZZER_MIN_FREQUENCY_HZ) ||
        (frequency_hz > ATLAS_BUZZER_MAX_FREQUENCY_HZ) ||
        (timer_tick_hz == 0U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }
    counts = (timer_tick_hz + (frequency_hz / 2U)) / frequency_hz;
    if ((counts < 2U) || (counts > 65536U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    (void)HAL_TIM_PWM_Stop(buzzer->timer, TIM_CHANNEL_1);
    (void)HAL_TIM_PWM_Stop(buzzer->timer, TIM_CHANNEL_2);
    __HAL_TIM_SET_AUTORELOAD(buzzer->timer, counts - 1U);
    __HAL_TIM_SET_COUNTER(buzzer->timer, 0U);

    channel.Pulse = counts / 2U;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    channel.OCIdleState = TIM_OCIDLESTATE_RESET;
    channel.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    channel.OCMode = TIM_OCMODE_PWM1;
    if (HAL_TIM_PWM_ConfigChannel(buzzer->timer, &channel, TIM_CHANNEL_1) != HAL_OK)
    {
        return ATLAS_ERROR_IO;
    }
    /* PWM2 produces the exact logical inverse of PWM1 at the same compare point. */
    channel.OCMode = TIM_OCMODE_PWM2;
    if (HAL_TIM_PWM_ConfigChannel(buzzer->timer, &channel, TIM_CHANNEL_2) != HAL_OK)
    {
        return ATLAS_ERROR_IO;
    }
    buzzer->frequency_hz = timer_tick_hz / counts;
    return ATLAS_OK;
}

/**
 * @brief Bind TIM15 and configure opposite-phase channels with no audible output.
 * @param buzzer Destination driver instance.
 * @param timer Initialized TIM15 PWM handle.
 * @return ATLAS_OK or a typed timer/clock failure.
 */
AtlasStatus AtlasBuzzer_Init(AtlasBuzzer *buzzer, TIM_HandleTypeDef *timer)
{
    AtlasStatus status;

    if ((buzzer == NULL) || (timer == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (timer->Instance != TIM15)
    {
        return ATLAS_ERROR_ARGUMENT;
    }
    memset(buzzer, 0, sizeof(*buzzer));
    buzzer->timer = timer;
    buzzer->timer_clock_hz = atlas_buzzer_timer_clock_hz();
    if (buzzer->timer_clock_hz == 0U)
    {
        return ATLAS_ERROR_IO;
    }
    status = atlas_buzzer_configure(buzzer, ATLAS_BUZZER_RESONANT_FREQUENCY_HZ);
    if (status == ATLAS_OK)
    {
        buzzer->initialized = true;
        AtlasBuzzer_Stop(buzzer);
    }
    return status;
}

/**
 * @brief Start a continuous differential square wave.
 * @param buzzer Initialized buzzer instance.
 * @param frequency_hz Requested output frequency.
 * @return ATLAS_OK or a typed argument/timer failure.
 */
AtlasStatus AtlasBuzzer_Start(AtlasBuzzer *buzzer, uint32_t frequency_hz)
{
    AtlasStatus status;

    if (buzzer == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!buzzer->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    status = atlas_buzzer_configure(buzzer, frequency_hz);
    if (status != ATLAS_OK)
    {
        return status;
    }
    if (HAL_TIM_PWM_Start(buzzer->timer, TIM_CHANNEL_1) != HAL_OK)
    {
        return ATLAS_ERROR_IO;
    }
    if (HAL_TIM_PWM_Start(buzzer->timer, TIM_CHANNEL_2) != HAL_OK)
    {
        (void)HAL_TIM_PWM_Stop(buzzer->timer, TIM_CHANNEL_1);
        return ATLAS_ERROR_IO;
    }
    buzzer->running = true;
    buzzer->timed = false;
    return ATLAS_OK;
}

/**
 * @brief Start a tone that stops automatically when serviced.
 * @param buzzer Initialized buzzer instance.
 * @param frequency_hz Requested output frequency.
 * @param duration_ms Nonzero duration shorter than 2^31 milliseconds.
 * @return ATLAS_OK or a typed argument/timer failure.
 */
AtlasStatus AtlasBuzzer_Beep(AtlasBuzzer *buzzer,
                             uint32_t frequency_hz,
                             uint32_t duration_ms)
{
    AtlasStatus status;

    if ((duration_ms == 0U) || (duration_ms >= 0x80000000UL))
    {
        return ATLAS_ERROR_ARGUMENT;
    }
    status = AtlasBuzzer_Start(buzzer, frequency_hz);
    if (status == ATLAS_OK)
    {
        buzzer->stop_at_ms = HAL_GetTick() + duration_ms;
        buzzer->timed = true;
    }
    return status;
}

/**
 * @brief Stop both TIM15 channels, producing zero differential drive.
 * @param buzzer Initialized buzzer instance; NULL is ignored.
 */
void AtlasBuzzer_Stop(AtlasBuzzer *buzzer)
{
    if ((buzzer != NULL) && (buzzer->timer != NULL))
    {
        (void)HAL_TIM_PWM_Stop(buzzer->timer, TIM_CHANNEL_1);
        (void)HAL_TIM_PWM_Stop(buzzer->timer, TIM_CHANNEL_2);
        buzzer->running = false;
        buzzer->timed = false;
    }
}

/**
 * @brief Stop an expired nonblocking beep.
 * @param buzzer Initialized buzzer instance; NULL is ignored.
 */
void AtlasBuzzer_Service(AtlasBuzzer *buzzer)
{
    if ((buzzer != NULL) && buzzer->running && buzzer->timed &&
        ((int32_t)(HAL_GetTick() - buzzer->stop_at_ms) >= 0))
    {
        AtlasBuzzer_Stop(buzzer);
    }
}
