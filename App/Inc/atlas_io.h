/**
 * @file atlas_io.h
 * @brief Owned ADC, expansion GPIO, KST PWM and bounded pyro services.
 *
 * Major functions:
 * - AtlasIo_Start(): starts continuous read-only monitoring; every output stays OFF.
 * - AtlasIo_Submit()/Receive(): copied, nonblocking commands and ticketed results.
 * - AtlasIo_GetSnapshot(): coherent voltages, lead indications and output diagnostics.
 * - AtlasIo_EmergencyStop(): immediate register-only, per-boot output inhibition.
 *
 * No command parser or flight decision is included. An application must explicitly
 * supply qualified settings, enable/arm, and request actions. GPIO-low/PWM-disabled
 * is the electrical default, NOT a guarantee of a safe mechanical servo position.
 */
#ifndef ATLAS_IO_H
#define ATLAS_IO_H
#include "main.h"
#include "atlas_analog.h"
#include "atlas_pyro_policy.h"
#include "atlas_status.h"
#include <stdbool.h>
#include <stdint.h>
#define ATLAS_IO_PWM_CHANNELS (8U)
#define ATLAS_IO_GPIO_CHANNELS (7U)
#define ATLAS_IO_QUEUE_CAPACITY (8U)
#define ATLAS_IO_COMMAND_MAX_AGE_MS (50U)
#define ATLAS_IO_BENCH_GPIO_MS (1000U)
/** @brief Exclusively transferred peripheral handles; TIM2 remains shared by GNSS/BNO. */
typedef struct
{
    ADC_HandleTypeDef *adc_external, *adc_internal;
    TIM_HandleTypeDef *pwm_1_to_4, *pwm_5_to_8, *pyro_timer;
    DMA_HandleTypeDef *pyro_dma;
} AtlasIoHardware;
/** @brief Measured mechanical limits, within the KST electrical 900-2100 us envelope. */
typedef struct { uint16_t minimum_us, neutral_us, maximum_us; } AtlasPwmCalibration;
/** @brief Explicit application qualification record; zero initialization disables outputs.
 * @note The physical arm terminal has no direct digital sense; qualified ADC voltage
 *       is a supply-present proxy. These booleans are declarations, not certification. */
typedef struct
{
    bool electrical_review_complete, pyro_cutoff_qualified, pyro_enabled;
    uint8_t pwm_allowed_mask, gpio_high_allowed_mask;
    AtlasPwmCalibration pwm[ATLAS_IO_PWM_CHANNELS];
    uint32_t arm_minimum_mv, arm_maximum_mv;
    uint16_t continuity_open_max_permille, continuity_closed_min_permille;
} AtlasOutputConfiguration;
/** @brief Commands do not directly expose timer, ADC, DMA or GPIO handles. */
typedef enum
{
    ATLAS_IO_CONFIGURE = 0, ATLAS_IO_PWM_ENABLE, ATLAS_IO_PWM_SET,
    ATLAS_IO_PWM_DISABLE, ATLAS_IO_GPIO_SET, ATLAS_IO_PYRO_ARM,
    ATLAS_IO_PYRO_DISARM, ATLAS_IO_PYRO_REQUEST,
    ATLAS_IO_BENCH_GPIO /* Diagnostic image only, fixed 1 s logic-level pulse. */
} AtlasIoCommandType;
/** @brief Completely copied command; channels are ZERO-BASED, masks use bit 0 for connector 1. */
typedef struct
{
    AtlasIoCommandType type;
    union
    {
        AtlasOutputConfiguration configuration;
        struct { uint8_t channel; uint16_t pulse_us; } pwm;
        struct { uint8_t channel; bool high; } gpio;
        uint8_t channel_mask;
        uint8_t pyro_channel;
    } arguments;
} AtlasIoCommand;
/** @brief One command outcome, not proof of a physical result or successful deployment. */
typedef struct { uint32_t ticket; AtlasIoCommandType type; AtlasStatus status; } AtlasIoResult;
/** @brief First failed operation in the independent ADC3 VREF/temperature state machine. */
typedef enum
{
    ATLAS_IO_REFERENCE_FAILURE_NONE = 0,
    ATLAS_IO_REFERENCE_FAILURE_CONFIGURE,
    ATLAS_IO_REFERENCE_FAILURE_START,
    ATLAS_IO_REFERENCE_FAILURE_OVERRUN,
    ATLAS_IO_REFERENCE_FAILURE_TIMEOUT,
    ATLAS_IO_REFERENCE_FAILURE_POLL,
    ATLAS_IO_REFERENCE_FAILURE_STOP,
    ATLAS_IO_REFERENCE_FAILURE_RAW_RANGE,
    ATLAS_IO_REFERENCE_FAILURE_VDDA_RANGE,
    ATLAS_IO_REFERENCE_FAILURE_TEMPERATURE_RANGE
} AtlasIoReferenceFailureStage;
/** @brief Coherent service diagnostics; last physical pulse events remain independently bounded. */
typedef struct
{
    AtlasAnalogSample analog;
    AtlasPyroPolicy pyro;
    AtlasContinuity continuity[ATLAS_PYRO_CHANNELS];
    uint16_t commanded_pwm_us[ATLAS_IO_PWM_CHANNELS];
    uint32_t heartbeat, published_at_ms, adc_errors, command_rejections;
    uint32_t stack_free_words, reset_flags, last_ticket;
    uint32_t power_events, ecc_events, ecc_monitor_register, ecc_failing_word, ecc_error_code;
    uint32_t reference_raw, reference_hal_status, reference_hal_error;
    AtlasIoReferenceFailureStage reference_failure_stage;
    AtlasStatus status, last_command_status;
    uint8_t gpio_inputs, gpio_commanded_high, pwm_enabled_mask;
    bool reference_temperature_channel;
    bool external_switch, arm_supply_present, configured, emergency_latched;
} AtlasIoSnapshot;
/** @brief Initialize private DMA memory/calibration, then create the static owner.
 * @param hardware Initialized generated handles copied by value.
 * @return Atlas status; called once, before scheduling. Outputs are not enabled. */
AtlasStatus AtlasIo_Start(const AtlasIoHardware *hardware);
/** @brief Queue one copied command without waiting. @param command Request.
 * @param ticket Optional ticket destination. @return Accepted/busy/invalid status.
 * @note Task-only; acceptance is not execution. Commands older than 50 ms cannot
 *       assert an output. Drain Receive(), or result backpressure stops command work. */
AtlasStatus AtlasIo_Submit(const AtlasIoCommand *command, uint32_t *ticket);
/** @brief Consume one retained completion. @param result Destination.
 * @return true if copied, false on empty/invalid/wrong context. One designated consumer. */
bool AtlasIo_Receive(AtlasIoResult *result);
/** @brief Read a coherent snapshot, without blocking. @param snapshot Destination.
 * @return true if available in task context. Check timestamps and validity before use. */
bool AtlasIo_GetSnapshot(AtlasIoSnapshot *snapshot);
/** @brief Immediately inhibit PWM/pyro/GPIO, permanently for this boot.
 * @note Task, ISR and fatal-fault safe; no RTOS, allocation, HAL locks or waits.
 *       Pyro pins first become inputs so a late DMA SET cannot reassert a gate.
 *       A queued DISARM is not this emergency path. External energy isolation is
 *       still required; firmware cannot protect against all hardware failures. */
void AtlasIo_EmergencyStop(void);
/** @brief Internal board IRQ adapter for the additional D0TCM ECC monitor; no RTOS calls. */
void AtlasIo_HandleDtcm0Irq(void);
#endif
