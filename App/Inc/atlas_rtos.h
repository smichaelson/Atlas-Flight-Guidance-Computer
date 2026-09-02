/**
 * @file atlas_rtos.h
 * @brief Static FreeRTOS integration, sensor snapshots, commands, and supervision for Atlas.
 *
 * Major functions:
 * - AtlasRtos_Start(): creates all static RTOS objects and transfers control to the scheduler.
 * - AtlasRtos_GetSnapshot(): copies the latest coherent, multi-sensor data publication.
 * - AtlasRtos_SubmitCommand(): queues bounded LED, buzzer, radio, or BLE work for the I/O owner.
 * - AtlasRtos_ReadRadio()/AtlasRtos_ReadBle(): consume RTOS-owned transparent receive streams.
 * - AtlasRtos_GetHealth(): reports task liveness, stack margins, faults, and command outcomes.
 * - AtlasRtos_ApplicationStep(): weak 100 Hz control-algorithm integration hook.
 */

#ifndef ATLAS_RTOS_H
#define ATLAS_RTOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atlas_board.h"
#include "atlas_rtos_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_RTOS_COMMAND_PAYLOAD_CAPACITY       (64U)
#define ATLAS_RTOS_COMMAND_QUEUE_CAPACITY          (8U)
#define ATLAS_RTOS_RX_STREAM_CAPACITY           (1024U)
#define ATLAS_RTOS_MAX_STREAM_TX_TIMEOUT_MS       (10U)
#define ATLAS_RTOS_MAX_QUEUE_WAIT_MS               (5U)

/** @brief Validity bits for fields in AtlasRtosSnapshot. */
typedef enum
{
    ATLAS_RTOS_VALID_ADXL375                = (1UL << 0),
    ATLAS_RTOS_VALID_LSM6DSV16B             = (1UL << 1),
    ATLAS_RTOS_VALID_MMC5983MA              = (1UL << 2),
    ATLAS_RTOS_VALID_MS5611                 = (1UL << 3),
    ATLAS_RTOS_VALID_BNO_ACCELEROMETER      = (1UL << 4),
    ATLAS_RTOS_VALID_BNO_GYROSCOPE          = (1UL << 5),
    ATLAS_RTOS_VALID_BNO_MAGNETOMETER       = (1UL << 6),
    ATLAS_RTOS_VALID_BNO_ROTATION_VECTOR    = (1UL << 7),
    ATLAS_RTOS_VALID_GNSS_NAV_PVT           = (1UL << 8),
    ATLAS_RTOS_VALID_GNSS_PPS               = (1UL << 9)
} AtlasRtosSnapshotValid;

/** @brief Sensors whose absence or staleness prevents watchdog refresh. */
#define ATLAS_RTOS_REQUIRED_SENSOR_MASK \
    (ATLAS_RTOS_VALID_ADXL375 | ATLAS_RTOS_VALID_LSM6DSV16B | \
     ATLAS_RTOS_VALID_MMC5983MA | ATLAS_RTOS_VALID_MS5611 | \
     ATLAS_RTOS_VALID_BNO_ACCELEROMETER | ATLAS_RTOS_VALID_BNO_GYROSCOPE | \
     ATLAS_RTOS_VALID_BNO_MAGNETOMETER | ATLAS_RTOS_VALID_BNO_ROTATION_VECTOR | \
     ATLAS_RTOS_VALID_GNSS_NAV_PVT)

/** @brief Coherent publication assembled exclusively by the I/O owner task. */
typedef struct
{
    uint32_t sequence;
    uint32_t published_at_ms;
    uint32_t valid_mask;
    AtlasStatus board_service_status;
    AtlasStatus adxl375_status;
    AtlasStatus lsm6dsv16b_status;
    AtlasStatus mmc5983ma_status;
    AtlasStatus ms5611_status;
    AtlasAdxl375Sample adxl375;
    AtlasLsm6dsv16bSample lsm6dsv16b;
    AtlasMmc5983maField mmc5983ma;
    AtlasMs5611Sample ms5611;
    sh2_SensorValue_t bno_accelerometer;
    sh2_SensorValue_t bno_gyroscope;
    sh2_SensorValue_t bno_magnetometer;
    sh2_SensorValue_t bno_rotation_vector;
    uint32_t bno_accelerometer_received_at_ms;
    uint32_t bno_gyroscope_received_at_ms;
    uint32_t bno_magnetometer_received_at_ms;
    uint32_t bno_rotation_vector_received_at_ms;
    AtlasGnssNavPvt gnss_nav_pvt;
    AtlasGnssPps gnss_pps;
    uint32_t sensor_recovery_until_ms;
    bool radio_command_mode;
    bool ble_command_mode;
    bool ble_dtr_asserted;
    bool maintenance_active;
    bool sensor_recovery_active;
} AtlasRtosSnapshot;

/** @brief Work types accepted by the RTOS I/O owner. */
typedef enum
{
    ATLAS_RTOS_COMMAND_LED_SET = 0,
    ATLAS_RTOS_COMMAND_BUZZER_BEEP,
    ATLAS_RTOS_COMMAND_BUZZER_STOP,
    ATLAS_RTOS_COMMAND_RADIO_WRITE,
    ATLAS_RTOS_COMMAND_BLE_WRITE,
    ATLAS_RTOS_COMMAND_RADIO_ENTER_COMMAND_MODE,
    ATLAS_RTOS_COMMAND_RADIO_EXIT_COMMAND_MODE,
    ATLAS_RTOS_COMMAND_BLE_ENTER_COMMAND_MODE,
    ATLAS_RTOS_COMMAND_BLE_ENTER_DATA_MODE,
    ATLAS_RTOS_COMMAND_RADIO_READ_IDENTITY,
    ATLAS_RTOS_COMMAND_RADIO_READ_SETTINGS,
    ATLAS_RTOS_COMMAND_RADIO_SET_PARAMETER,
    ATLAS_RTOS_COMMAND_RADIO_HOST_BAUD,
    ATLAS_RTOS_COMMAND_BLE_CONFIGURE_SPS
} AtlasRtosCommandType;

/** @brief Bounded stream payload carried by one queued radio or BLE request. */
typedef struct
{
    uint8_t bytes[ATLAS_RTOS_COMMAND_PAYLOAD_CAPACITY];
    uint16_t length;
    uint32_t timeout_ms;
} AtlasRtosStreamCommand;

/** @brief Caller-owned request copied completely into the static command queue. */
typedef struct
{
    AtlasRtosCommandType type;
    union
    {
        AtlasLedColor led_color;
        struct
        {
            uint32_t frequency_hz;
            uint32_t duration_ms;
        } buzzer;
        AtlasRtosStreamCommand stream;
        struct { uint8_t index; uint32_t value; bool persist; } radio_parameter;
        uint32_t radio_host_baud;
        struct { char name[30]; bool persist; } ble_profile;
    } arguments;
} AtlasRtosCommandRequest;

/** @brief Retained reply to a radio identity/settings query, including failures.
 * @note Other commands report through CommandCompleted/GetHealth. Queries require
 *       explicit command mode first; this text is untrusted modem data. */
typedef struct
{
    uint32_t ticket;
    AtlasRtosCommandType type;
    AtlasStatus status;
    char text[ATLAS_RFD900X_RESPONSE_CAPACITY];
} AtlasRtosMaintenanceReply;

/** @brief Scheduler lifecycle visible to diagnostics and telemetry. */
typedef enum
{
    ATLAS_RTOS_STATE_STOPPED = 0,
    ATLAS_RTOS_STATE_STARTING,
    ATLAS_RTOS_STATE_RUNNING,
    ATLAS_RTOS_STATE_FAULTED
} AtlasRtosState;

/** @brief Coherent RTOS health snapshot. */
typedef struct
{
    AtlasRtosState state;
    AtlasRtosFault fault;
    AtlasStatus startup_status;
    AtlasStatus service_status;
    AtlasStatus sampling_status;
    AtlasStatus last_command_status;
    AtlasRtosCommandType last_command_type;
    uint32_t last_command_ticket;
    uint32_t io_heartbeat;
    uint32_t application_heartbeat;
    uint32_t supervisor_cycles;
    uint32_t watchdog_refreshes;
    uint32_t snapshot_publications;
    uint32_t command_submissions;
    uint32_t command_rejections;
    uint32_t commands_completed;
    uint32_t command_failures;
    uint32_t radio_rx_dropped_bytes;
    uint32_t ble_rx_dropped_bytes;
    uint32_t io_deadline_misses;
    uint32_t application_deadline_misses;
    uint32_t maximum_application_lateness_ticks;
    uint32_t application_resynchronizations;
    uint32_t sensor_freshness_checks;
    uint32_t sensor_recovery_windows;
    uint32_t stale_sensor_mask;
    uint32_t started_at_ms;
    uint32_t io_stack_free_words;
    uint32_t application_stack_free_words;
    uint32_t supervisor_stack_free_words;
    uint32_t idle_stack_free_words;
    uint32_t io_busy_until_ms;
    uint32_t assert_line;
    const char *assert_file;
    bool io_busy;
} AtlasRtosHealth;

/**
 * @brief Create all static RTOS resources and start preemptive scheduling.
 * @param board Fully initialized Atlas board; becomes exclusively owned by the I/O task.
 * @param watchdog Initialized independent watchdog refreshed only by the supervisor.
 * @param startup_status Aggregate result returned by AtlasBoard_Init().
 * @return ATLAS_ERROR_* only if validation/object creation fails or the scheduler returns.
 * @note This function does not return during healthy operation.
 */
AtlasStatus AtlasRtos_Start(AtlasBoard *board,
                            IWDG_HandleTypeDef *watchdog,
                            AtlasStatus startup_status);

/**
 * @brief Copy the latest complete sensor publication.
 * @param snapshot Destination snapshot.
 * @param timeout_ms Mutex wait from zero through ATLAS_RTOS_MAX_QUEUE_WAIT_MS.
 * @return true when a coherent copy was acquired from task context.
 */
bool AtlasRtos_GetSnapshot(AtlasRtosSnapshot *snapshot, uint32_t timeout_ms);

/**
 * @brief Copy coherent scheduler, watchdog, stack, and command diagnostics.
 * @param health Destination health record.
 * @param timeout_ms Mutex wait from zero through ATLAS_RTOS_MAX_QUEUE_WAIT_MS.
 * @return true when a coherent copy was acquired from task context.
 */
bool AtlasRtos_GetHealth(AtlasRtosHealth *health, uint32_t timeout_ms);

/**
 * @brief Queue one validated request for execution by the sole hardware-I/O owner.
 * @param request Request copied by value; payload buffers may be reused after return.
 * @param timeout_ms Queue wait from zero through ATLAS_RTOS_MAX_QUEUE_WAIT_MS.
 * @param ticket Optional destination for the monotonic command ticket.
 * @return ATLAS_OK when queued, or a typed argument/readiness/busy failure.
 * @note Call only from task context. Acceptance is not execution success; observe
 *       AtlasRtos_CommandCompleted() or the latest result in AtlasRtosHealth.
 */
AtlasStatus AtlasRtos_SubmitCommand(const AtlasRtosCommandRequest *request,
                                    uint32_t timeout_ms,
                                    uint32_t *ticket);

/** @brief Consume a retained identity/settings reply without blocking.
 * @param reply Destination. @return true if copied in task context.
 * @note One consumer must drain this four-entry queue; a queued query waits if
 *       replies are full. No automatic module/NVM commissioning is performed. */
bool AtlasRtos_ReadMaintenanceReply(AtlasRtosMaintenanceReply *reply);

/**
 * @brief Read bytes received from the transparent RFD900x flight-link stream.
 * @param destination Destination buffer.
 * @param capacity Maximum bytes to copy.
 * @param timeout_ms Receive wait from zero through ATLAS_RTOS_MAX_QUEUE_WAIT_MS.
 * @return Number of bytes copied; zero on invalid input, timeout, or wrong context.
 */
size_t AtlasRtos_ReadRadio(uint8_t *destination,
                           size_t capacity,
                           uint32_t timeout_ms);

/**
 * @brief Read bytes received from the NINA-B112 transparent BLE stream.
 * @param destination Destination buffer.
 * @param capacity Maximum bytes to copy.
 * @param timeout_ms Receive wait from zero through ATLAS_RTOS_MAX_QUEUE_WAIT_MS.
 * @return Number of bytes copied; zero on invalid input, timeout, or wrong context.
 */
size_t AtlasRtos_ReadBle(uint8_t *destination,
                         size_t capacity,
                         uint32_t timeout_ms);

/**
 * @brief Report whether the Atlas scheduler is currently running.
 * @return true only after successful object creation and scheduler startup.
 */
bool AtlasRtos_IsRunning(void);

/** @brief Report the conservative output gate (no startup grace or stale samples).
 * @return true only after fresh sensors, healthy supervision and no maintenance.
 * @note This is necessary, not sufficient, for PWM/pyro authorization. */
bool AtlasRtos_OutputsPermitted(void);
/** @brief Permanently inhibit outputs for this boot, from task or fault context. */
void AtlasRtos_InhibitOutputs(void);

/**
 * @brief Return a stable diagnostic name for an RTOS fault.
 * @param fault Fault value.
 * @return Constant string; never NULL.
 */
const char *AtlasRtos_FaultName(AtlasRtosFault fault);

/**
 * @brief Application control hook called at 100 Hz with a coherent snapshot.
 * @param snapshot Read-only snapshot valid only for the duration of the call.
 * @param now_ms Current HAL monotonic millisecond tick.
 * @note Override this weak function in one application source file. It must return
 *       before the next period, must not call hardware drivers, and may submit only
 *       bounded AtlasRtos commands.
 */
void AtlasRtos_ApplicationStep(const AtlasRtosSnapshot *snapshot,
                               uint32_t now_ms);

/**
 * @brief Optional completion hook invoked in I/O-task context after a command executes.
 * @param ticket Ticket returned by AtlasRtos_SubmitCommand().
 * @param type Completed command type.
 * @param status Driver result for the operation.
 * @note Override only with a nonblocking implementation that never calls a driver.
 */
void AtlasRtos_CommandCompleted(uint32_t ticket,
                                AtlasRtosCommandType type,
                                AtlasStatus status);

/**
 * @brief Record an RTOS assertion and wait for the independent watchdog reset.
 * @param file Static source-file string supplied by configASSERT.
 * @param line Source line supplied by configASSERT.
 * @note This terminal function disables interrupts and does not return.
 */
void AtlasRtos_AssertFailed(const char *file, uint32_t line);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_RTOS_H */
