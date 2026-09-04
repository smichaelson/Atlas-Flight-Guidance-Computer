/**
 * @file atlas_io.c
 * @brief Static monitoring/output owner and STM32H743 rev-0.1 hardware adapter.
 *
 * Major functions:
 * - io_task()/io_sample(): service asynchronous ADC scans and publish fresh data.
 * - io_execute(): validate qualified, expiring application commands at execution.
 * - io_pyro_start()/io_pyro_stop(): serialize timer/DMA pulses; no task-timed high pin.
 * - AtlasIo_EmergencyStop(): remove output drive even if a DMA request is pending.
 * - Public queue/snapshot functions: preserve ownership and bounded application work.
 *
 * TIM6's first overflow DMA-sets one gate; the next overflow, 500 ms later, resets
 * all five. CPU halting before timer launch leaves gates low. After launch both
 * edges are peripheral-driven. This is NOT a redundant hardware cutoff: clocks,
 * DMA, supply, GPIO and the electrical load require inert bench qualification.
 */
#include "atlas_io.h"
#include "atlas_build.h"
#if ATLAS_BRINGUP
#include "atlas_usb.h"
#endif
#include "atlas_rtos.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stddef.h>
#include <string.h>

#define IO_STACK_WORDS (1536U)
#define IO_PERIOD_MS (5U)
#define IO_ADC_TIMEOUT_MS (20U)
#define IO_REFERENCE_PERIOD_MS (100U)
#define IO_REFERENCE_MAX_AGE_MS (250U)
#define PYRO_PINS (PYRO_FIRE1_Pin | PYRO_FIRE2_Pin | PYRO_FIRE3_Pin | PYRO_FIRE4_Pin | PYRO_FIRE5_Pin)
#define PYRO_MODER_MASK ((3UL << 18) | (3UL << 20) | (3UL << 22) | (3UL << 24) | (3UL << 26))
#define PYRO_MODER_OUTPUT ((1UL << 18) | (1UL << 20) | (1UL << 22) | (1UL << 24) | (1UL << 26))

/** @brief DMA memory initialized in complete ECC words before any subword DMA access. */
typedef union { uint64_t ecc[4]; uint32_t words[8]; uint16_t samples[16]; } IoDmaBlock;
#if defined(__ICCARM__)
#pragma location = ".atlas_dma"
#pragma data_alignment = 32
static __no_init IoDmaBlock adc_buffer;
#pragma location = ".atlas_dma"
#pragma data_alignment = 32
static __no_init IoDmaBlock pyro_buffer;
#elif defined(__GNUC__)
static IoDmaBlock adc_buffer __attribute__((section(".atlas_dma"), aligned(32)));
static IoDmaBlock pyro_buffer __attribute__((section(".atlas_dma"), aligned(32)));
#else
#error "Define and verify Atlas DMA placement for this compiler before enabling IO."
#endif

typedef struct { AtlasIoCommand command; uint32_t ticket, submitted_ms, epoch; } IoQueued;
static AtlasIoHardware hw;
static AtlasIoSnapshot working, published;
static AtlasOutputConfiguration settings;
static QueueHandle_t requests, results;
static StaticQueue_t requests_control, results_control;
static uint8_t request_memory[ATLAS_IO_QUEUE_CAPACITY * sizeof(IoQueued)];
static uint8_t result_memory[ATLAS_IO_QUEUE_CAPACITY * sizeof(AtlasIoResult)];
static StaticTask_t task_control;
static StackType_t task_stack[IO_STACK_WORDS];
static bool started, configured_locked, external_pending, internal_pending;
static bool reference_valid, last_permitted, internal_temperature;
static uint32_t external_started_ms, internal_started_ms, reference_started_ms;
static uint32_t reference_vdda, next_ticket, output_epoch, adc_pulse_epoch;
static uint8_t continuity_streak[ATLAS_PYRO_CHANNELS];
static AtlasContinuity continuity_candidate[ATLAS_PYRO_CHANNELS];
static volatile bool hardware_ready, emergency_latched;
static volatile bool adc_complete, adc_failed, pulse_active, pulse_complete, pulse_failed;
static volatile uint32_t pulse_epoch;
#if ATLAS_BRINGUP
static uint32_t bench_gpio_started_ms;
static bool bench_gpio_active;
#endif
static volatile uint32_t power_events, ecc_events, ecc_monitor_register, ecc_failing_word, ecc_error_code;
static RAMECC_HandleTypeDef dtcm0_monitor;
extern RAMECC_HandleTypeDef hramecc1_m1, hramecc1_m4, hramecc2_m1, hramecc3_m1;

/** @brief Save interrupt mask for short register-only task/ISR critical sections.
 * @return Previous PRIMASK. */
static uint32_t io_lock(void) { uint32_t mask = __get_PRIMASK(); __disable_irq(); __DMB(); return mask; }
/** @brief Restore saved interrupt state. @param mask Previous PRIMASK. */
static void io_unlock(uint32_t mask) { __DMB(); __set_PRIMASK(mask); }
/** @brief Check task-only public API preconditions. @return Correct calling context. */
static bool io_context(void)
{ return started && __get_IPSR() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING; }

/** @brief Deassert only the seven expansion output pins, never sensor chip selects. */
static void io_gpio_low(void)
{
    GPIOF->BSRR = (uint32_t)(GPIO_OUT1_Pin | GPIO_OUT6_Pin | GPIO_OUT7_Pin) << 16;
    GPIOE->BSRR = (uint32_t)GPIO_OUT2_Pin << 16;
    GPIOA->BSRR = (uint32_t)(GPIO_OUT3_Pin | GPIO_OUT4_Pin) << 16;
    GPIOC->BSRR = (uint32_t)GPIO_OUT5_Pin << 16;
}
/** @brief Change one owned expansion GPIO and its command record.
 * @param channel Zero-based output. @param high Requested level. @return Bounds/status. */
static AtlasStatus io_gpio_command(uint8_t channel, bool high)
{
    static GPIO_TypeDef *const ports[7] = {GPIOF,GPIOE,GPIOA,GPIOA,GPIOC,GPIOF,GPIOF};
    static const uint16_t pins[7] = {GPIO_OUT1_Pin,GPIO_OUT2_Pin,GPIO_OUT3_Pin,GPIO_OUT4_Pin,GPIO_OUT5_Pin,GPIO_OUT6_Pin,GPIO_OUT7_Pin};
    if (channel >= ATLAS_IO_GPIO_CHANNELS) return ATLAS_ERROR_ARGUMENT;
    HAL_GPIO_WritePin(ports[channel], pins[channel], high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    if (high) working.gpio_commanded_high |= (uint8_t)(1U << channel);
    else working.gpio_commanded_high &= (uint8_t)~(1U << channel);
    return ATLAS_OK;
}
/** @brief Select GPIO-low (disabled) or the generated timer AF for one PWM pin.
 * @param channel Zero-based channel. @param alternate True to use its timer output. */
static void io_pwm_pin(uint32_t channel, bool alternate)
{
    static GPIO_TypeDef *const ports[8] = {GPIOE,GPIOE,GPIOE,GPIOE,GPIOC,GPIOC,GPIOB,GPIOB};
    static const uint8_t shifts[8] = {18U,22U,26U,28U,12U,14U,0U,2U};
    const uint32_t shift = shifts[channel];
    ports[channel]->BSRR = (1UL << (shift / 2U)) << 16;
    MODIFY_REG(ports[channel]->MODER, 3UL << shift, (alternate ? 2UL : 1UL) << shift);
}
/** @brief Non-recoverable electrical inhibition, independent of scheduler progress. */
void AtlasIo_EmergencyStop(void)
{
    const uint32_t mask = io_lock();
    emergency_latched = true;
    if (hardware_ready)
    {
        /* Disable the driver before a pending DMA write can set ODR again.
         * External gate pull-downs then keep PD9..13 deasserted. */
        CLEAR_BIT(GPIOD->MODER, PYRO_MODER_MASK);
        TIM6->DIER = 0U;
        CLEAR_BIT(TIM6->CR1, TIM_CR1_CEN);
        CLEAR_BIT(DMA1_Stream1->CR, DMA_SxCR_EN);
        GPIOD->BSRR = (uint32_t)PYRO_PINS << 16;
        CLEAR_BIT(TIM1->BDTR, TIM_BDTR_MOE);
        TIM1->CCER = 0U;
        TIM3->CCER = 0U;
        CLEAR_BIT(TIM1->CR1, TIM_CR1_CEN);
        CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
        for (uint32_t i = 0; i < ATLAS_IO_PWM_CHANNELS; ++i) io_pwm_pin(i, false);
        io_gpio_low();
    }
    io_unlock(mask);
}
/** @brief Preserve the first monitor/adapter error and inhibit all actuation.
 * @param status Failure reason. */
static void io_fail(AtlasStatus status)
{
    if (working.status == ATLAS_OK) working.status = status;
    AtlasRtos_InhibitOutputs();
}
/** @brief Retain the first ADC3 reference/temperature failure without changing policy.
 * @param stage Failed state-machine operation.
 * @param temperature true for the temperature channel, false for VREFINT.
 * @param hal_status HAL result associated with the operation.
 * @param raw Most recent ADC data register value, or zero before a read. */
static void io_reference_note_failure(AtlasIoReferenceFailureStage stage,
                                      bool temperature,
                                      HAL_StatusTypeDef hal_status,
                                      uint32_t raw)
{
    if (working.reference_failure_stage != ATLAS_IO_REFERENCE_FAILURE_NONE) return;
    working.reference_failure_stage = stage;
    working.reference_temperature_channel = temperature;
    working.reference_hal_status = (uint32_t)hal_status;
    working.reference_hal_error = hw.adc_internal != NULL ?
                                  (uint32_t)hw.adc_internal->ErrorCode : 0U;
    working.reference_raw = raw;
}
/** @brief ADC DMA completion carries flags only. @param adc Completed handle. */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{ if (hardware_ready && adc == hw.adc_external) { __DMB(); adc_complete = true; } }
/** @brief ADC/DMA errors prevent later stale-buffer use. @param adc Failed handle. */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{ if (hardware_ready && adc == hw.adc_external) { adc_failed = true; AtlasIo_EmergencyStop(); } }
/** @brief Brownout notification; recovery of voltage never rearms outputs. */
void HAL_PWR_PVDCallback(void)
{
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_PVDO))
    { ++power_events; AtlasRtos_InhibitOutputs(); }
}
/** @brief Preserve first ECC evidence and inhibit even a corrected single error.
 * @param monitor Monitor serviced by the shared IRQ; HAL also calls idle monitors. */
void HAL_RAMECC_DetectErrorCallback(RAMECC_HandleTypeDef *monitor)
{
    if (monitor == NULL || monitor->RAMECCErrorCode == HAL_RAMECC_ERROR_NONE) return;
    if (ecc_events == 0U)
    {
        ecc_monitor_register = (uint32_t)monitor->Instance;
        ecc_failing_word = HAL_RAMECC_GetFailingAddress(monitor);
        ecc_error_code = monitor->RAMECCErrorCode;
    }
    ++ecc_events;
    /* HAL accumulates this code and invokes callbacks for idle monitors too.
     * Consume it after preserving evidence so a later shared IRQ is not counted twice. */
    monitor->RAMECCErrorCode = HAL_RAMECC_ERROR_NONE;
    AtlasRtos_InhibitOutputs();
}
/** @brief Route the additional D0TCM monitor not selected in the original CubeMX file. */
void AtlasIo_HandleDtcm0Irq(void)
{ if (dtcm0_monitor.Instance != NULL) HAL_RAMECC_IRQHandler(&dtcm0_monitor); }
/** @brief Timer DMA completed both SET and RESET words. @param dma Completed stream. */
static void io_pulse_done(DMA_HandleTypeDef *dma)
{
    if (dma != hw.pyro_dma) return;
    TIM6->DIER = 0U;
    CLEAR_BIT(TIM6->CR1, TIM_CR1_CEN);
    GPIOD->BSRR = (uint32_t)PYRO_PINS << 16;
    pulse_active = false;
    ++pulse_epoch;
    __DMB();
    pulse_complete = true;
}
/** @brief DMA transfer failure inhibits the physical output immediately. @param dma Stream. */
static void io_pulse_error(DMA_HandleTypeDef *dma)
{ if (dma == hw.pyro_dma) { pulse_failed = true; AtlasIo_EmergencyStop(); } }

/** @brief Stop/quiesce before buffer reuse; called only by the output owner.
 * @return true only when the stream can no longer write any GPIO register. */
static bool io_pyro_stop(void)
{
    /* Remove drive BEFORE disabling the scheduled hardware reset. A CPU halt
     * anywhere after this first store must not leave a high gate without cutoff. */
    CLEAR_BIT(GPIOD->MODER, PYRO_MODER_MASK);
    TIM6->DIER = 0U;
    CLEAR_BIT(TIM6->CR1, TIM_CR1_CEN);
    if (hw.pyro_dma->State == HAL_DMA_STATE_BUSY && HAL_DMA_Abort(hw.pyro_dma) != HAL_OK)
    { pulse_failed = true; io_fail(ATLAS_ERROR_IO); return false; }
    if ((DMA1_Stream1->CR & DMA_SxCR_EN) != 0U)
    { pulse_failed = true; io_fail(ATLAS_ERROR_IO); return false; }
    __DSB();
    GPIOD->BSRR = (uint32_t)PYRO_PINS << 16;
    const uint32_t mask = io_lock();
    if (!emergency_latched) MODIFY_REG(GPIOD->MODER, PYRO_MODER_MASK, PYRO_MODER_OUTPUT);
    io_unlock(mask);
    if (pulse_active) ++pulse_epoch;
    pulse_active = pulse_complete = false;
    return true;
}
/** @brief Prepare two DMA words and launch a timer-owned 500 ms pulse.
 * @param channel Authorized zero-based channel. @return Hardware launch status. */
static AtlasStatus io_pyro_start(uint8_t channel)
{
    if (channel >= ATLAS_PYRO_CHANNELS || pulse_active || emergency_latched ||
        (DMA1_Stream1->CR & DMA_SxCR_EN) != 0U) return ATLAS_ERROR_STATE;
    TIM6->DIER = 0U;
    TIM6->CR1 = 0U; /* In particular, OPM and URS must not suppress either DMA event. */
    TIM6->PSC = 999U; /* Verified 100 MHz timer kernel /1000 = 100 kHz. */
    TIM6->ARR = 49999U;
    TIM6->EGR = TIM_EGR_UG; /* Load prescaler while UDE is OFF: no phantom SET. */
    TIM6->SR = 0U;
    TIM6->CNT = 49999U; /* First update after one tick, second exactly 500 ms later. */
    pyro_buffer.ecc[0] = ((uint64_t)((uint32_t)PYRO_PINS << 16) << 32) |
                          ((uint32_t)PYRO_FIRE1_Pin << channel);
    __DSB();
    pulse_complete = pulse_failed = false;
    hw.pyro_dma->XferCpltCallback = io_pulse_done;
    hw.pyro_dma->XferHalfCpltCallback = NULL;
    hw.pyro_dma->XferErrorCallback = io_pulse_error;
    hw.pyro_dma->XferAbortCallback = NULL;
    if (HAL_DMA_Start_IT(hw.pyro_dma, (uint32_t)pyro_buffer.words,
                        (uint32_t)&GPIOD->BSRR, 2U) != HAL_OK) return ATLAS_ERROR_IO;
    const uint32_t mask = io_lock();
    if (emergency_latched || !AtlasRtos_OutputsPermitted())
    {
        io_unlock(mask);
        (void)io_pyro_stop();
        return ATLAS_ERROR_STATE;
    }
    /* The final CEN store is the only launch. No CPU instruction sets a gate. */
    pulse_active = true;
    ++pulse_epoch;
    __HAL_DBGMCU_UnFreeze_TIM6();
    TIM6->DIER = TIM_DIER_UDE;
    SET_BIT(TIM6->CR1, TIM_CR1_CEN);
    io_unlock(mask);
    return ATLAS_OK;
}

/** @brief Start one ADC3 channel; never overwrite a two-rank DR before reading it.
 * @param temperature Select temperature rather than VREFINT. @return HAL result. */
static HAL_StatusTypeDef io_internal_start(bool temperature)
{
    ADC_ChannelConfTypeDef channel = {0};
    HAL_StatusTypeDef hal_status;

    channel.Channel = temperature ? ADC_CHANNEL_TEMPSENSOR : ADC_CHANNEL_VREFINT;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_810CYCLES_5;
    channel.SingleDiff = ADC_SINGLE_ENDED;
    channel.OffsetNumber = ADC_OFFSET_NONE;
    hal_status = HAL_ADC_ConfigChannel(hw.adc_internal, &channel);
    if (hal_status != HAL_OK)
    {
        io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_CONFIGURE,
                                  temperature, hal_status, 0U);
        return hal_status;
    }
    internal_temperature = temperature;
    internal_started_ms = HAL_GetTick();
    hal_status = HAL_ADC_Start(hw.adc_internal);
    if (hal_status != HAL_OK)
    {
        io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_START,
                                  temperature, hal_status, 0U);
        return hal_status;
    }
    internal_pending = true;
    return HAL_OK;
}
/** @brief Poll completed ADC3 work without busy waiting. @param now Current tick. */
static void io_reference(uint32_t now)
{
    if (internal_pending)
    {
        if (__HAL_ADC_GET_FLAG(hw.adc_internal, ADC_FLAG_OVR))
        {
            io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_OVERRUN,
                                      internal_temperature, HAL_ERROR, 0U);
            (void)HAL_ADC_Stop(hw.adc_internal);
            reference_valid = false;
            io_fail(ATLAS_ERROR_TIMEOUT);
            return;
        }
        if ((uint32_t)(now - internal_started_ms) > IO_ADC_TIMEOUT_MS)
        {
            io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_TIMEOUT,
                                      internal_temperature, HAL_TIMEOUT, 0U);
            (void)HAL_ADC_Stop(hw.adc_internal);
            reference_valid = false;
            io_fail(ATLAS_ERROR_TIMEOUT);
            return;
        }
        if (!__HAL_ADC_GET_FLAG(hw.adc_internal, ADC_FLAG_EOC)) return;
        const HAL_StatusTypeDef poll_status = HAL_ADC_PollForConversion(hw.adc_internal, 0U);
        if (poll_status != HAL_OK)
        {
            io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_POLL,
                                      internal_temperature, poll_status, 0U);
            reference_valid = false;
            io_fail(ATLAS_ERROR_IO);
            return;
        }
        const uint32_t raw = HAL_ADC_GetValue(hw.adc_internal);
        working.reference_raw = raw;
        working.reference_temperature_channel = internal_temperature;
        const HAL_StatusTypeDef stop_status = HAL_ADC_Stop(hw.adc_internal);
        if (stop_status != HAL_OK)
        {
            io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_STOP,
                                      internal_temperature, stop_status, raw);
            reference_valid = false;
            io_fail(ATLAS_ERROR_IO);
            return;
        }
        if (raw < 16U || raw >= 65472U)
        {
            io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_RAW_RANGE,
                                      internal_temperature, HAL_OK, raw);
            reference_valid = false;
            io_fail(ATLAS_ERROR_IO);
            return;
        }
        internal_pending = false;
        if (!internal_temperature)
        {
            reference_vdda = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(raw, ADC_RESOLUTION_16B);
            reference_valid = reference_vdda >= 2800U && reference_vdda <= 3600U;
            working.analog.reference_at_ms = internal_started_ms;
            if (!reference_valid)
            {
                io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_VDDA_RANGE,
                                          false, HAL_OK, raw);
                io_fail(ATLAS_ERROR_IO);
            }
            else if (io_internal_start(true) != HAL_OK)
            {
                io_fail(ATLAS_ERROR_IO);
            }
        }
        else
        {
            const int32_t temperature = __HAL_ADC_CALC_TEMPERATURE(reference_vdda, raw, ADC_RESOLUTION_16B);
            if (temperature < -50 || temperature > 150)
            {
                io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_TEMPERATURE_RANGE,
                                          true, HAL_OK, raw);
                io_fail(ATLAS_ERROR_IO);
                return;
            }
            working.analog.die_temperature_c = (int16_t)temperature;
        }
    }
    else if (!reference_valid || (uint32_t)(now - reference_started_ms) >= IO_REFERENCE_PERIOD_MS)
    {
        reference_started_ms = now;
        if (io_internal_start(false) != HAL_OK) io_fail(ATLAS_ERROR_IO);
    }
}
/** @brief Decode only a completed, quiescent external scan and launch the next one.
 * @param now Current tick. @return true if a new complete scan was published. */
static bool io_sample(uint32_t now)
{
    bool updated = false;
    io_reference(now);
    if (working.status != ATLAS_OK || emergency_latched) return false;
    if (external_pending)
    {
        if (adc_failed || (uint32_t)(now - external_started_ms) > IO_ADC_TIMEOUT_MS)
        {
            /* Keep the private buffer quarantined after ANY failed stop; never
             * return/reuse caller memory or reset another DMA1 stream. */
            (void)HAL_ADC_Stop_DMA(hw.adc_external);
            ++working.adc_errors;
            io_fail(ATLAS_ERROR_IO);
            return false;
        }
        if (!adc_complete) return false;
        if (HAL_ADC_Stop_DMA(hw.adc_external) != HAL_OK ||
            (DMA1_Stream0->CR & DMA_SxCR_EN) != 0U || adc_failed)
        { ++working.adc_errors; io_fail(ATLAS_ERROR_IO); return false; }
        __DSB();
        ++working.analog.sequence;
        working.analog.sampled_at_ms = external_started_ms;
        AtlasAnalog_Convert(&working.analog, adc_buffer.samples, reference_vdda,
            reference_valid && (uint32_t)(now - working.analog.reference_at_ms) <= IO_REFERENCE_MAX_AGE_MS);
        /* A scan crossing either gate edge must not be interpreted as lead loss. */
        if (pulse_active || adc_pulse_epoch != pulse_epoch)
            working.analog.valid_mask &= (uint16_t)0x1FU;
        external_pending = false;
        updated = true;
    }
    adc_complete = adc_failed = false;
    external_started_ms = now;
    adc_pulse_epoch = pulse_epoch;
    if (HAL_ADC_Start_DMA(hw.adc_external, adc_buffer.words, ATLAS_ANALOG_CHANNELS) != HAL_OK)
    { ++working.adc_errors; io_fail(ATLAS_ERROR_IO); return updated; }
    external_pending = true;
    return updated;
}

/** @brief Qualify a fresh measured rail value. @param index ADC rank index.
 * @param now Tick. @param minimum Lower mV. @param maximum Upper mV. @return In range. */
static bool io_rail(uint32_t index, uint32_t now, uint32_t minimum, uint32_t maximum)
{
    return (working.analog.valid_mask & (1U << index)) != 0U &&
        (uint32_t)(now - working.analog.sampled_at_ms) <= ATLAS_PYRO_MAX_SAMPLE_AGE_MS &&
        (uint32_t)(now - working.analog.reference_at_ms) <= IO_REFERENCE_MAX_AGE_MS &&
        working.analog.millivolts[index] >= minimum && working.analog.millivolts[index] <= maximum;
}
/** @brief Continuously classify/debounce leads; no valid arm supply means UNKNOWN.
 * @param now Tick. @param updated Advance debounce only on a NEW ADC sample. */
static void io_continuity(uint32_t now, bool updated)
{
    working.arm_supply_present = working.configured && settings.pyro_enabled &&
        io_rail(ATLAS_ANALOG_ARM_SUPPLY, now, settings.arm_minimum_mv, settings.arm_maximum_mv);
    for (uint32_t i = 0; i < ATLAS_PYRO_CHANNELS; ++i)
    {
        const AtlasContinuity value = AtlasPyroPolicy_Classify(
            working.analog.millivolts[ATLAS_ANALOG_ARM_SUPPLY],
            working.analog.millivolts[ATLAS_ANALOG_CONTINUITY_1 + i],
            working.arm_supply_present &&
                (working.analog.valid_mask & (1U << (ATLAS_ANALOG_CONTINUITY_1 + i))) != 0U,
            pulse_active, settings.continuity_open_max_permille, settings.continuity_closed_min_permille);
        if (value == ATLAS_CONTINUITY_UNKNOWN)
        { working.continuity[i] = value; continuity_streak[i] = 0U; }
        else if (updated)
        {
            if (value != continuity_candidate[i]) { continuity_candidate[i] = value; continuity_streak[i] = 0U; }
            if (continuity_streak[i] < 3U) ++continuity_streak[i];
            working.continuity[i] = continuity_streak[i] >= 3U ? value : ATLAS_CONTINUITY_UNKNOWN;
        }
    }
}
/** @brief Build policy inputs from current monitored/interlocked state. @param now Tick.
 * @return Fresh input record; continuity does not authorize software arming by itself. */
static AtlasPyroInput io_pyro_input(uint32_t now)
{
    AtlasPyroInput input = {0};
    input.now_ms = now;
    input.sample_ms = working.analog.sampled_at_ms;
    input.interlocks_ok = !emergency_latched && AtlasRtos_OutputsPermitted() &&
        working.configured && settings.pyro_enabled && settings.pyro_cutoff_qualified && working.arm_supply_present;
    input.sample_valid = io_rail(ATLAS_ANALOG_3V3, now, 3000U, 3600U);
    input.pulse_active = pulse_active;
    input.pulse_complete = pulse_complete;
    input.pulse_fault = pulse_failed || emergency_latched;
    memcpy(input.continuity, working.continuity, sizeof(input.continuity));
    return input;
}
/** @brief Disable PWM without inventing a safe servo position. @param bits Channels. */
static void io_pwm_disable(uint8_t bits)
{
    for (uint32_t i = 0U; i < ATLAS_IO_PWM_CHANNELS; ++i)
        if ((bits & working.pwm_enabled_mask & (1U << i)) != 0U)
        {
            io_pwm_pin(i, false);
            (void)HAL_TIM_PWM_Stop(i < 4U ? hw.pwm_1_to_4 : hw.pwm_5_to_8, (i % 4U) * 4U);
            working.commanded_pwm_us[i] = 0U;
        }
    working.pwm_enabled_mask &= (uint8_t)~bits;
}
/** @brief Return all commanded outputs to their electrical defaults; budgets survive. */
static void io_stop_all(void)
{
    (void)io_pyro_stop();
    AtlasPyroPolicy_Disarm(&working.pyro, HAL_GetTick());
    io_pwm_disable(UINT8_MAX);
    io_gpio_low();
    working.gpio_commanded_high = 0U;
    ++output_epoch;
}
/** @brief Check explicit engineering limits, without silently filling in guesses.
 * @param c Configuration. @return true if all requested outputs have bounded settings. */
static bool io_configuration_valid(const AtlasOutputConfiguration *c)
{
    if (!c->electrical_review_complete || (c->gpio_high_allowed_mask & 0x80U) != 0U) return false;
    for (uint32_t i = 0; i < ATLAS_IO_PWM_CHANNELS; ++i)
        if ((c->pwm_allowed_mask & (1U << i)) != 0U &&
            (c->pwm[i].minimum_us < 900U || c->pwm[i].maximum_us > 2100U ||
             c->pwm[i].minimum_us > c->pwm[i].neutral_us || c->pwm[i].neutral_us > c->pwm[i].maximum_us)) return false;
    if (c->pyro_enabled && (!c->pyro_cutoff_qualified || c->arm_minimum_mv < 1000U ||
        c->arm_maximum_mv > 30000U || c->arm_maximum_mv <= c->arm_minimum_mv ||
        c->continuity_open_max_permille > 400U || c->continuity_closed_min_permille < 600U ||
        c->continuity_closed_min_permille > 1000U)) return false;
    return true;
}
/** @brief Execute a queued, current command exclusively in the output task.
 * @param queued Retained request and age/generation. @return Execution status. */
static AtlasStatus io_execute_unlocked(const IoQueued *queued)
{
    const AtlasIoCommand *command = &queued->command;
    const uint32_t now = HAL_GetTick();
    if (command->type == ATLAS_IO_PWM_DISABLE)
    { io_pwm_disable(command->arguments.channel_mask); ++output_epoch; return ATLAS_OK; }
    if (command->type == ATLAS_IO_PYRO_DISARM)
    { const bool safe = io_pyro_stop(); AtlasPyroPolicy_Disarm(&working.pyro, HAL_GetTick()); ++output_epoch; return safe ? ATLAS_OK : ATLAS_ERROR_IO; }
    if (command->type == ATLAS_IO_GPIO_SET && !command->arguments.gpio.high)
    {
        /* Deassertion is allowed even with expired health or unqualified settings.
         * Fence older queued HIGH commands; this is still an asynchronous request. */
        const AtlasStatus status = io_gpio_command(command->arguments.gpio.channel, false);
        if (status == ATLAS_OK) ++output_epoch;
        return status;
    }
#if ATLAS_BRINGUP
    /* A separate, fixed-duration diagnostic path touches only the seven logic
     * GPIOs. There is deliberately no bench override of PWM or pyro permission. */
    if (command->type == ATLAS_IO_BENCH_GPIO)
    {
        if (!command->arguments.gpio.high)
        {
            io_gpio_low(); working.gpio_commanded_high = 0U;
            bench_gpio_active = false; ++output_epoch;
            return ATLAS_OK;
        }
        AtlasUsbHealth usb;
        if (queued->epoch != output_epoch ||
            (uint32_t)(now - queued->submitted_ms) > ATLAS_IO_COMMAND_MAX_AGE_MS ||
            emergency_latched || working.status != ATLAS_OK || bench_gpio_active ||
            !io_rail(ATLAS_ANALOG_3V3, now, 3000U, 3500U) ||
            !AtlasUsb_GetHealth(&usb) || !usb.configured || !usb.dtr)
            return ATLAS_ERROR_STATE;
        const AtlasStatus status = io_gpio_command(command->arguments.gpio.channel, true);
        if (status == ATLAS_OK) { bench_gpio_started_ms = now; bench_gpio_active = true; }
        return status;
    }
    return ATLAS_ERROR_UNSUPPORTED;
#endif
    if (command->type == ATLAS_IO_CONFIGURE)
    {
        if (configured_locked || emergency_latched || !io_configuration_valid(&command->arguments.configuration)) return ATLAS_ERROR_STATE;
        settings = command->arguments.configuration;
        working.configured = true;
        ++output_epoch; /* Commands submitted against old limits do not carry over. */
        memset(continuity_streak, 0, sizeof(continuity_streak));
        return ATLAS_OK;
    }
    /* Fail closed on delayed commands or a permission cycle between submit/execute. */
    if (queued->epoch != output_epoch || (uint32_t)(now - queued->submitted_ms) > ATLAS_IO_COMMAND_MAX_AGE_MS ||
        !working.configured || emergency_latched || !AtlasRtos_OutputsPermitted() ||
        !io_rail(ATLAS_ANALOG_3V3, now, 3000U, 3600U)) return ATLAS_ERROR_STATE;
    const uint8_t channel = command->arguments.pwm.channel;
    switch (command->type)
    {
        case ATLAS_IO_PWM_ENABLE:
        {
            const uint8_t bits = command->arguments.channel_mask;
            if (bits == 0U || (bits & (uint8_t)~settings.pwm_allowed_mask) != 0U ||
                !io_rail(ATLAS_ANALOG_PWM_SUPPLY, now, 4800U, 8400U)) return ATLAS_ERROR_ARGUMENT;
            for (uint32_t i = 0; i < ATLAS_IO_PWM_CHANNELS; ++i)
                if ((bits & (1U << i)) != 0U && (working.pwm_enabled_mask & (1U << i)) == 0U)
                {
                    TIM_HandleTypeDef *timer = i < 4U ? hw.pwm_1_to_4 : hw.pwm_5_to_8;
                    __HAL_TIM_SET_COMPARE(timer, (i % 4U) * 4U, settings.pwm[i].neutral_us);
                    io_pwm_pin(i, true);
                    if (HAL_TIM_PWM_Start(timer, (i % 4U) * 4U) != HAL_OK)
                    { io_fail(ATLAS_ERROR_IO); return ATLAS_ERROR_IO; }
                    working.commanded_pwm_us[i] = settings.pwm[i].neutral_us;
                    working.pwm_enabled_mask |= (uint8_t)(1U << i);
                }
            configured_locked = true;
            return ATLAS_OK;
        }
        case ATLAS_IO_PWM_SET:
            if (channel >= ATLAS_IO_PWM_CHANNELS || (working.pwm_enabled_mask & (1U << channel)) == 0U ||
                command->arguments.pwm.pulse_us < settings.pwm[channel].minimum_us ||
                command->arguments.pwm.pulse_us > settings.pwm[channel].maximum_us) return ATLAS_ERROR_ARGUMENT;
            __HAL_TIM_SET_COMPARE(channel < 4U ? hw.pwm_1_to_4 : hw.pwm_5_to_8,
                                  (channel % 4U) * 4U, command->arguments.pwm.pulse_us);
            working.commanded_pwm_us[channel] = command->arguments.pwm.pulse_us;
            return ATLAS_OK;
        case ATLAS_IO_GPIO_SET:
        {
            const uint8_t i = command->arguments.gpio.channel;
            if (i >= ATLAS_IO_GPIO_CHANNELS || (settings.gpio_high_allowed_mask & (1U << i)) == 0U)
                return ATLAS_ERROR_ARGUMENT;
            configured_locked = true;
            return io_gpio_command(i, true);
        }
        case ATLAS_IO_PYRO_ARM:
        {
            const AtlasPyroInput input = io_pyro_input(now);
            configured_locked = true;
            return AtlasPyroPolicy_Arm(&working.pyro, &input);
        }
        case ATLAS_IO_PYRO_REQUEST:
        {
            const AtlasPyroInput input = io_pyro_input(now);
            return AtlasPyroPolicy_Request(&working.pyro, &input, command->arguments.pyro_channel);
        }
        default: return ATLAS_ERROR_ARGUMENT;
    }
}

/** @brief Exclude a safety ISR only during short output-asserting register work.
 * @param queued Request. @return Execution status.
 * @note DISARM may wait for DMA quiescence, so it MUST keep interrupts enabled. */
static AtlasStatus io_execute(const IoQueued *queued)
{
    if (queued->command.type == ATLAS_IO_PYRO_DISARM ||
        queued->command.type == ATLAS_IO_PWM_DISABLE || queued->command.type == ATLAS_IO_CONFIGURE)
        return io_execute_unlocked(queued);
    const uint32_t mask = io_lock();
    const AtlasStatus status = io_execute_unlocked(queued);
    io_unlock(mask);
    return status;
}

/** @brief Poll seven inputs and the external switch with their generated pulls. */
static void io_inputs(void)
{
    static GPIO_TypeDef *const ports[7] = {GPIOE,GPIOE,GPIOG,GPIOE,GPIOE,GPIOE,GPIOE};
    static const uint16_t pins[7] = {GPIO_IN1_Pin,GPIO_IN2_Pin,GPIO_IN3_Pin,GPIO_IN4_Pin,GPIO_IN5_Pin,GPIO_IN6_Pin,GPIO_IN7_Pin};
    working.gpio_inputs = 0U;
    for (uint32_t i = 0; i < ATLAS_IO_GPIO_CHANNELS; ++i)
        if (HAL_GPIO_ReadPin(ports[i], pins[i]) == GPIO_PIN_SET) working.gpio_inputs |= (uint8_t)(1U << i);
    working.external_switch = HAL_GPIO_ReadPin(EXT_SWITCH_GPIO_Port, EXT_SWITCH_Pin) == GPIO_PIN_SET;
}
#if ATLAS_BRINGUP
/** @brief Expire a diagnostic logic pulse independently of USB command processing.
 * @param now Current tick; subtraction remains valid across uint32 wrap. */
static void io_bench_gpio_service(uint32_t now)
{
    if (!bench_gpio_active) return;
    AtlasUsbHealth usb;
    if ((uint32_t)(now - bench_gpio_started_ms) >= ATLAS_IO_BENCH_GPIO_MS ||
        emergency_latched || !io_rail(ATLAS_ANALOG_3V3, now, 3000U, 3500U) ||
        !AtlasUsb_GetHealth(&usb) || !usb.configured || !usb.dtr)
    {
        io_gpio_low(); working.gpio_commanded_high = 0U;
        bench_gpio_active = false; ++output_epoch;
    }
}
#endif
/** @brief Own continuous monitoring and at most one bounded command per cycle.
 * @param argument Unused. */
static void io_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    bool emergency_handled = false;
    for (;;)
    {
        const uint32_t now = HAL_GetTick();
        const bool updated = !emergency_latched && working.status == ATLAS_OK && io_sample(now);
        io_inputs();
        io_continuity(now, updated);
#if ATLAS_BRINGUP
        io_bench_gpio_service(now);
#endif
        const bool permitted = !emergency_latched && AtlasRtos_OutputsPermitted() && io_rail(ATLAS_ANALOG_3V3, now, 3000U, 3600U);
        if (last_permitted && !permitted) io_stop_all();
        last_permitted = permitted;
        if (working.pwm_enabled_mask != 0U && !io_rail(ATLAS_ANALOG_PWM_SUPPLY, now, 4800U, 8400U))
        { io_pwm_disable(UINT8_MAX); ++output_epoch; }
        if (emergency_latched && !emergency_handled)
        {
            AtlasIo_EmergencyStop();
            io_stop_all();
            working.pyro.fault_latched = true;
            AtlasPyroPolicy_Disarm(&working.pyro, HAL_GetTick());
            if (working.status == ATLAS_OK) working.status = ATLAS_ERROR_STATE;
            emergency_handled = true;
        }
        IoQueued queued;
        /* Reserve completion space before consuming a command. Monitoring and
         * pulse cutoff never wait for a client to drain results. */
        if (uxQueueSpacesAvailable(results) != 0U && xQueueReceive(requests, &queued, 0U) == pdTRUE)
        {
            const AtlasStatus status = io_execute(&queued);
            AtlasIoResult result = {queued.ticket, queued.command.type, status};
            working.last_ticket = queued.ticket;
            working.last_command_status = status;
            if (status != ATLAS_OK) ++working.command_rejections;
            configASSERT(xQueueSend(results, &result, 0U) == pdTRUE);
        }
        const AtlasPyroInput input = io_pyro_input(HAL_GetTick());
        const AtlasPyroAction action = AtlasPyroPolicy_Step(&working.pyro, &input);
        if (action == ATLAS_PYRO_ACTION_START)
        {
            const AtlasStatus status = io_pyro_start(working.pyro.channel);
            if (status != ATLAS_OK) { pulse_failed = true; io_fail(status); }
        }
        else if (action == ATLAS_PYRO_ACTION_STOP)
        {
            (void)io_pyro_stop();
            if (working.pyro.phase != ATLAS_PYRO_EXHAUSTED)
                AtlasPyroPolicy_Disarm(&working.pyro, HAL_GetTick());
            if (working.pyro.fault_latched) io_fail(ATLAS_ERROR_IO);
        }
        working.emergency_latched = emergency_latched;
        working.power_events = power_events;
        working.ecc_events = ecc_events;
        working.ecc_monitor_register = ecc_monitor_register;
        working.ecc_failing_word = ecc_failing_word;
        working.ecc_error_code = ecc_error_code;
        working.published_at_ms = HAL_GetTick();
        ++working.heartbeat;
        working.stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        taskENTER_CRITICAL();
        published = working;
        taskEXIT_CRITICAL();
        /* Skip missed releases instead of a CPU-consuming catch-up burst. */
        if ((TickType_t)(xTaskGetTickCount() - wake) >= pdMS_TO_TICKS(IO_PERIOD_MS)) wake = xTaskGetTickCount();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(IO_PERIOD_MS));
    }
}

/** @brief Initialize private buffers, calibrated ADCs and static service objects.
 * @param hardware Generated handles. @return Startup status, always outputs-off. */
AtlasStatus AtlasIo_Start(const AtlasIoHardware *hardware)
{
    if (hardware == NULL || hardware->adc_external == NULL || hardware->adc_internal == NULL ||
        hardware->pwm_1_to_4 == NULL || hardware->pwm_5_to_8 == NULL ||
        hardware->pyro_timer == NULL || hardware->pyro_dma == NULL) return ATLAS_ERROR_NULL;
    if (started || emergency_latched || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) return ATLAS_ERROR_STATE;
    if (hardware->adc_external->Instance != ADC1 || hardware->adc_internal->Instance != ADC3 ||
        hardware->pwm_1_to_4->Instance != TIM1 || hardware->pwm_5_to_8->Instance != TIM3 ||
        hardware->pyro_timer->Instance != TIM6 || hardware->pyro_dma->Instance != DMA1_Stream1 ||
        hardware->adc_external->DMA_Handle == NULL || hardware->adc_external->DMA_Handle->Instance != DMA1_Stream0)
        return ATLAS_ERROR_ARGUMENT;
    /* Deliberately fail closed if clocks or the cache policy change. Cache-on DMA
     * needs its own MPU/coherency implementation and renewed qualification. */
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U || HAL_RCC_GetHCLKFreq() != 100000000U ||
        HAL_RCC_GetPCLK1Freq() != 50000000U || HAL_RCC_GetPCLK2Freq() != 50000000U ||
        (RCC->CFGR & RCC_CFGR_TIMPRE) != 0U ||
        (uintptr_t)&adc_buffer < UINT32_C(0x24000000) || (uintptr_t)(&adc_buffer + 1) > UINT32_C(0x24080000) ||
        (uintptr_t)&pyro_buffer < UINT32_C(0x24000000) || (uintptr_t)(&pyro_buffer + 1) > UINT32_C(0x24080000)) return ATLAS_ERROR_STATE;
    hw = *hardware;
    hardware_ready = true;
    working.reset_flags = RCC->RSR; /* Read only: do not erase debugger/reset provenance. */
    for (uint32_t i = 0; i < 4U; ++i)
    { ((volatile uint64_t *)adc_buffer.ecc)[i] = 0U; ((volatile uint64_t *)pyro_buffer.ecc)[i] = 0U; }
    __DSB();
    AtlasPyroPolicy_Init(&working.pyro);
    if (!io_pyro_stop()) return ATLAS_ERROR_IO;
    io_gpio_low();
    for (uint32_t i = 0; i < ATLAS_IO_PWM_CHANNELS; ++i) io_pwm_pin(i, false);
    /* Both interleaved DTCM banks and the used AXI SRAM are monitored.
     * Only seeded memory is read; never probe uninitialized RAM to test ECC. */
    dtcm0_monitor.Instance = RAMECC1_Monitor3;
    if (HAL_RAMECC_Init(&dtcm0_monitor) != HAL_OK) return ATLAS_ERROR_IO;
    RAMECC_HandleTypeDef *const monitors[] = {&hramecc1_m1, &dtcm0_monitor, &hramecc1_m4, &hramecc2_m1, &hramecc3_m1};
    for (uint32_t i = 0; i < sizeof(monitors) / sizeof(monitors[0]); ++i)
        if (HAL_RAMECC_StartMonitor(monitors[i]) != HAL_OK ||
            HAL_RAMECC_EnableNotification(monitors[i], RAMECC_IT_MONITOR_ALL) != HAL_OK)
        { AtlasIo_EmergencyStop(); return ATLAS_ERROR_IO; }
    /* One regular rank on ADC3, explicitly switched only after each stop/read. */
    hw.adc_internal->Init.ScanConvMode = ADC_SCAN_DISABLE;
    hw.adc_internal->Init.NbrOfConversion = 1U;
    hw.adc_internal->Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (*VREFINT_CAL_ADDR == 0U || *VREFINT_CAL_ADDR == UINT16_MAX ||
        *TEMPSENSOR_CAL1_ADDR == 0U || *TEMPSENSOR_CAL2_ADDR == 0U ||
        *TEMPSENSOR_CAL1_ADDR == UINT16_MAX || *TEMPSENSOR_CAL2_ADDR == UINT16_MAX ||
        *TEMPSENSOR_CAL1_ADDR == *TEMPSENSOR_CAL2_ADDR || HAL_ADC_Init(hw.adc_internal) != HAL_OK ||
        HAL_ADCEx_Calibration_Start(hw.adc_external, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED) != HAL_OK ||
        HAL_ADCEx_Calibration_Start(hw.adc_internal, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED) != HAL_OK)
    { AtlasIo_EmergencyStop(); return ATLAS_ERROR_IO; }
    hw.pyro_dma->Init.MemInc = DMA_MINC_ENABLE;
    if (HAL_DMA_Init(hw.pyro_dma) != HAL_OK) { AtlasIo_EmergencyStop(); return ATLAS_ERROR_IO; }
    /* Timer channels remain disabled. 100 MHz /100 /3003 ~= 333.0003 Hz. */
    TIM1->PSC = TIM3->PSC = 99U;
    TIM1->ARR = TIM3->ARR = 3002U;
    TIM1->EGR = TIM3->EGR = TIM_EGR_UG;
    TIM1->SR = TIM3->SR = 0U;
    requests = xQueueCreateStatic(ATLAS_IO_QUEUE_CAPACITY, sizeof(IoQueued), request_memory, &requests_control);
    results = xQueueCreateStatic(ATLAS_IO_QUEUE_CAPACITY, sizeof(AtlasIoResult), result_memory, &results_control);
    if (requests == NULL || results == NULL) return ATLAS_ERROR_STATE;
    if (xTaskCreateStatic(io_task, "AtlasOutputs", IO_STACK_WORDS, NULL, 6U, task_stack, &task_control) == NULL)
        return ATLAS_ERROR_STATE;
    started = true;
    return ATLAS_OK;
}
/** @brief Copy a task request and assign a unique ticket. @param command Request.
 * @param ticket Optional destination. @return Queue acceptance status. */
AtlasStatus AtlasIo_Submit(const AtlasIoCommand *command, uint32_t *ticket)
{
    if (command == NULL) return ATLAS_ERROR_NULL;
    if (!io_context()) return ATLAS_ERROR_STATE;
    if ((uint32_t)command->type > ATLAS_IO_BENCH_GPIO) return ATLAS_ERROR_ARGUMENT;
    if (!ATLAS_BRINGUP && command->type == ATLAS_IO_BENCH_GPIO) return ATLAS_ERROR_UNSUPPORTED;
    if (ATLAS_BRINGUP && command->type != ATLAS_IO_BENCH_GPIO &&
        command->type != ATLAS_IO_PWM_DISABLE && command->type != ATLAS_IO_PYRO_DISARM &&
        !(command->type == ATLAS_IO_GPIO_SET && !command->arguments.gpio.high)) return ATLAS_ERROR_UNSUPPORTED;
    IoQueued queued = {0};
    queued.command = *command;
    queued.submitted_ms = HAL_GetTick();
    taskENTER_CRITICAL();
    queued.ticket = ++next_ticket;
    queued.epoch = output_epoch;
    taskEXIT_CRITICAL();
    if (xQueueSend(requests, &queued, 0U) != pdTRUE) return ATLAS_ERROR_BUSY;
    if (ticket != NULL) *ticket = queued.ticket;
    return ATLAS_OK;
}
/** @brief Consume a retained result. @param result Destination. @return Availability. */
bool AtlasIo_Receive(AtlasIoResult *result)
{ return result != NULL && io_context() && xQueueReceive(results, result, 0U) == pdTRUE; }
/** @brief Copy published monitoring data. @param snapshot Destination. @return Success. */
bool AtlasIo_GetSnapshot(AtlasIoSnapshot *snapshot)
{
    if (snapshot == NULL || !io_context()) return false;
    taskENTER_CRITICAL();
    *snapshot = published;
    snapshot->emergency_latched = emergency_latched;
    taskEXIT_CRITICAL();
    return true;
}
