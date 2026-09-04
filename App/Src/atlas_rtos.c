/**
 * @file atlas_rtos.c
 * @brief Static RTOS task graph, hardware ownership, publication, and watchdog supervision.
 *
 * Major functions:
 * - AtlasRtos_Start(): validates NVIC grouping, builds static objects/tasks, and starts FreeRTOS.
 * - atlas_rtos_io_task(): owns sensor/link/expansion drivers and their queued commands.
 * - AtlasStorage/AtlasUsb/AtlasIo owners: service storage, USB, and electrical I/O separately.
 * - atlas_rtos_application_task(): calls the project control hook at 100 Hz on coherent data.
 * - atlas_rtos_supervisor_task(): refreshes IWDG only while tasks, sensors, and stacks are healthy.
 * - AtlasRtos_GetSnapshot()/GetHealth(): provide mutex-protected read-only publications.
 * - AtlasRtos_SubmitCommand()/ReadRadio()/ReadBle(): provide concurrency-safe application I/O.
 */

#include "atlas_rtos.h"
#include "atlas_build.h"
#include "atlas_storage.h"
#include "atlas_usb.h"
#include "atlas_io.h"
#include "atlas_expansion.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"

#define ATLAS_RTOS_IO_TASK_PRIORITY              (3U)
#define ATLAS_RTOS_APPLICATION_TASK_PRIORITY     (4U)
#define ATLAS_RTOS_SUPERVISOR_TASK_PRIORITY      (5U)
#define ATLAS_RTOS_IO_STACK_WORDS             (2048U)
#define ATLAS_RTOS_APPLICATION_STACK_WORDS     (1024U)
#define ATLAS_RTOS_SUPERVISOR_STACK_WORDS      (1024U)
#define ATLAS_RTOS_IDLE_STACK_WORDS             (256U)
#define ATLAS_RTOS_MIN_STACK_FREE_WORDS          (64U)
#define ATLAS_RTOS_IO_PERIOD_MS                    (1U)
#define ATLAS_RTOS_APPLICATION_PERIOD_MS          (10U)
#define ATLAS_RTOS_SUPERVISOR_PERIOD_MS          (100U)
#define ATLAS_RTOS_ADXL_PERIOD_MS                   (2U)
#define ATLAS_RTOS_LSM_PERIOD_MS                    (5U)
#define ATLAS_RTOS_MMC_PERIOD_MS                   (50U)
#define ATLAS_RTOS_MS5611_PERIOD_MS               (100U)
#define ATLAS_RTOS_IO_CYCLE_DEADLINE_MS            (20U)
#define ATLAS_RTOS_RADIO_MODE_DEADLINE_MS         (2200U)
#define ATLAS_RTOS_BLE_COMMAND_MODE_DEADLINE_MS   (3500U)
#define ATLAS_RTOS_BLE_DATA_MODE_DEADLINE_MS      (2000U)
#define ATLAS_RTOS_RADIO_QUERY_DEADLINE_MS        (1500U)
#define ATLAS_RTOS_RADIO_PARAMETER_DEADLINE_MS    (4000U)
#define ATLAS_RTOS_BLE_PROFILE_DEADLINE_MS       (35000U)
#define ATLAS_RTOS_SENSOR_STARTUP_GRACE_MS         (2000U)
#define ATLAS_RTOS_ADXL_MAX_AGE_MS                   (50U)
#define ATLAS_RTOS_LSM_MAX_AGE_MS                    (50U)
#define ATLAS_RTOS_MMC_MAX_AGE_MS                   (250U)
#define ATLAS_RTOS_MS5611_MAX_AGE_MS                (500U)
#define ATLAS_RTOS_BNO_MAX_AGE_MS                   (500U)
#define ATLAS_RTOS_GNSS_MAX_AGE_MS                 (1000U)
#define ATLAS_RTOS_SENSOR_RECOVERY_GRACE_MS         (1200U)

/** @brief Queue item including the project-assigned completion ticket. */
typedef struct
{
    AtlasRtosCommandRequest request;
    uint32_t ticket;
} AtlasRtosQueuedCommand;

/** @brief Entire statically allocated scheduler context. */
typedef struct
{
    AtlasBoard *board;
    IWDG_HandleTypeDef *watchdog;
    AtlasRtosSnapshot working_snapshot;
    AtlasRtosSnapshot published_snapshot;
    AtlasRtosHealth health;
    QueueHandle_t command_queue;
    StaticQueue_t command_queue_control;
    uint8_t command_queue_storage[ATLAS_RTOS_COMMAND_QUEUE_CAPACITY *
                                  sizeof(AtlasRtosQueuedCommand)];
    SemaphoreHandle_t snapshot_mutex;
    StaticSemaphore_t snapshot_mutex_control;
    SemaphoreHandle_t health_mutex;
    StaticSemaphore_t health_mutex_control;
    SemaphoreHandle_t radio_reader_mutex;
    StaticSemaphore_t radio_reader_mutex_control;
    SemaphoreHandle_t ble_reader_mutex;
    StaticSemaphore_t ble_reader_mutex_control;
    StreamBufferHandle_t radio_rx_stream;
    StaticStreamBuffer_t radio_rx_stream_control;
    uint8_t radio_rx_storage[ATLAS_RTOS_RX_STREAM_CAPACITY + 1U];
    StreamBufferHandle_t ble_rx_stream;
    StaticStreamBuffer_t ble_rx_stream_control;
    uint8_t ble_rx_storage[ATLAS_RTOS_RX_STREAM_CAPACITY + 1U];
    TaskHandle_t io_task;
    StaticTask_t io_task_control;
    StackType_t io_stack[ATLAS_RTOS_IO_STACK_WORDS];
    TaskHandle_t application_task;
    StaticTask_t application_task_control;
    StackType_t application_stack[ATLAS_RTOS_APPLICATION_STACK_WORDS];
    TaskHandle_t supervisor_task;
    StaticTask_t supervisor_task_control;
    StackType_t supervisor_stack[ATLAS_RTOS_SUPERVISOR_STACK_WORDS];
    uint32_t next_command_ticket;
    QueueHandle_t maintenance_replies;
    StaticQueue_t maintenance_reply_control;
    uint8_t maintenance_reply_memory[4U * sizeof(AtlasRtosMaintenanceReply)];
    AtlasRtosMaintenanceReply pending_reply;
    uint32_t started_at_ms;
    bool resources_ready;
} AtlasRtosContext;

static AtlasRtosContext atlas_rtos;
static volatile bool atlas_outputs_permitted;
static volatile bool atlas_output_fault;
static volatile bool atlas_maintenance_hold;
static volatile uint32_t atlas_output_fresh_until_ms;
static StaticTask_t atlas_rtos_idle_task_control;
static StackType_t atlas_rtos_idle_stack[ATLAS_RTOS_IDLE_STACK_WORDS];

/** @brief Read the independent, fail-closed output gate. @return Permission state. */
bool AtlasRtos_OutputsPermitted(void)
{
    const uint32_t remaining = atlas_output_fresh_until_ms - HAL_GetTick();
    return !ATLAS_BRINGUP && atlas_outputs_permitted && !atlas_output_fault && !atlas_maintenance_hold &&
           remaining != 0U && remaining < UINT32_C(0x80000000);
}

/** @brief Latch an output inhibition that no later healthy heartbeat clears. */
void AtlasRtos_InhibitOutputs(void)
{
    atlas_output_fault = true;
    atlas_outputs_permitted = false;
    __DMB();
    AtlasIo_EmergencyStop();
}

/**
 * @brief Convert a bounded millisecond timeout to at least one scheduler tick.
 * @param timeout_ms Millisecond timeout; zero remains nonblocking.
 * @return FreeRTOS tick count.
 */
static TickType_t atlas_rtos_timeout_ticks(uint32_t timeout_ms)
{
    TickType_t ticks;

    if (timeout_ms == 0U)
    {
        return 0U;
    }
    ticks = pdMS_TO_TICKS(timeout_ms);
    return (ticks == 0U) ? 1U : ticks;
}

/**
 * @brief Confirm that a public synchronization API is running in task context.
 * @return true only while scheduling in thread mode.
 */
static bool atlas_rtos_task_context(void)
{
    return atlas_rtos.resources_ready && (__get_IPSR() == 0U) &&
           (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
}

/**
 * @brief Preserve the first failing status in an aggregate.
 * @param aggregate Current aggregate status.
 * @param candidate New status.
 * @return candidate only when it is the first failure.
 */
static AtlasStatus atlas_rtos_accumulate(AtlasStatus aggregate,
                                          AtlasStatus candidate)
{
    return ((aggregate == ATLAS_OK) && (candidate != ATLAS_OK)) ?
           candidate : aggregate;
}

/**
 * @brief Update the latched sampling status and board runtime fault.
 * @param status Current sensor transaction result.
 */
static void atlas_rtos_record_sampling_failure(AtlasStatus status)
{
    if ((status == ATLAS_OK) || (status == ATLAS_ERROR_NOT_READY))
    {
        return;
    }
    if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
    {
        atlas_rtos.health.sampling_status =
            atlas_rtos_accumulate(atlas_rtos.health.sampling_status, status);
        xSemaphoreGive(atlas_rtos.health_mutex);
    }
    AtlasBoard_LatchRuntimeFault(atlas_rtos.board, status);
    AtlasRtos_InhibitOutputs();
}

/** @brief Earliest sample expiry for the lock-free output gate; no startup grace.
 * @param snapshot Owner's current sample set. @param now_ms Current tick.
 * @return Exclusive expiry tick; now_ms itself means inhibited. */
static uint32_t atlas_rtos_output_expiry(const AtlasRtosSnapshot *snapshot, uint32_t now_ms)
{
    if ((snapshot->valid_mask & ATLAS_RTOS_REQUIRED_SENSOR_MASK) != ATLAS_RTOS_REQUIRED_SENSOR_MASK ||
        snapshot->maintenance_active || snapshot->sensor_recovery_active) return now_ms;
    const uint32_t stamps[] = {snapshot->adxl375.timestamp_ms, snapshot->lsm6dsv16b.timestamp_ms,
        snapshot->mmc5983ma.timestamp_ms, snapshot->ms5611.timestamp_ms,
        snapshot->bno_accelerometer_received_at_ms, snapshot->bno_gyroscope_received_at_ms,
        snapshot->bno_magnetometer_received_at_ms, snapshot->bno_rotation_vector_received_at_ms,
        snapshot->gnss_nav_pvt.received_at_ms};
    const uint32_t limits[] = {ATLAS_RTOS_ADXL_MAX_AGE_MS, ATLAS_RTOS_LSM_MAX_AGE_MS,
        ATLAS_RTOS_MMC_MAX_AGE_MS, ATLAS_RTOS_MS5611_MAX_AGE_MS,
        ATLAS_RTOS_BNO_MAX_AGE_MS, ATLAS_RTOS_BNO_MAX_AGE_MS,
        ATLAS_RTOS_BNO_MAX_AGE_MS, ATLAS_RTOS_BNO_MAX_AGE_MS, ATLAS_RTOS_GNSS_MAX_AGE_MS};
    uint32_t remaining = UINT32_MAX;
    for (uint32_t i = 0; i < sizeof(stamps) / sizeof(stamps[0]); ++i)
    {
        const uint32_t age = now_ms - stamps[i];
        if (age >= limits[i]) return now_ms;
        if (limits[i] - age < remaining) remaining = limits[i] - age;
    }
    return now_ms + remaining;
}

/**
 * @brief Publish the I/O task's working snapshot as one atomic logical version.
 * @param now_ms Current HAL time.
 */
static void atlas_rtos_publish_snapshot(uint32_t now_ms)
{
    ++atlas_rtos.working_snapshot.sequence;
    atlas_rtos.working_snapshot.published_at_ms = now_ms;
    atlas_rtos.working_snapshot.radio_command_mode = atlas_rtos.board->radio.command_mode;
    atlas_rtos.working_snapshot.ble_command_mode = atlas_rtos.board->ble.command_mode;
    atlas_rtos.working_snapshot.ble_dtr_asserted =
        AtlasBle_IsDtrAsserted(&atlas_rtos.board->ble);
    /* A stalled sensor owner cannot keep outputs permitted until the next
     * 100 ms supervisor pass: the gate expires at the earliest sample limit. */
    atlas_output_fresh_until_ms = atlas_rtos_output_expiry(&atlas_rtos.working_snapshot, now_ms);

    if (xSemaphoreTake(atlas_rtos.snapshot_mutex, portMAX_DELAY) == pdTRUE)
    {
        atlas_rtos.published_snapshot = atlas_rtos.working_snapshot;
        xSemaphoreGive(atlas_rtos.snapshot_mutex);
    }
    if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
    {
        ++atlas_rtos.health.snapshot_publications;
        xSemaphoreGive(atlas_rtos.health_mutex);
    }
}

/**
 * @brief Capture newly decoded BNO085, GNSS, and PPS reports into the working snapshot.
 */
static void atlas_rtos_capture_protocol_samples(void)
{
    AtlasBoard *board = atlas_rtos.board;
    const uint32_t now_ms = HAL_GetTick();

    if (AtlasBoard_GetBno085Sample(board, SH2_ACCELEROMETER,
                                   &atlas_rtos.working_snapshot.bno_accelerometer,
                                   true))
    {
        atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_BNO_ACCELEROMETER;
        atlas_rtos.working_snapshot.bno_accelerometer_received_at_ms = now_ms;
    }
    if (AtlasBoard_GetBno085Sample(board, SH2_GYROSCOPE_CALIBRATED,
                                   &atlas_rtos.working_snapshot.bno_gyroscope,
                                   true))
    {
        atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_BNO_GYROSCOPE;
        atlas_rtos.working_snapshot.bno_gyroscope_received_at_ms = now_ms;
    }
    if (AtlasBoard_GetBno085Sample(board, SH2_MAGNETIC_FIELD_CALIBRATED,
                                   &atlas_rtos.working_snapshot.bno_magnetometer,
                                   true))
    {
        atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_BNO_MAGNETOMETER;
        atlas_rtos.working_snapshot.bno_magnetometer_received_at_ms = now_ms;
    }
    if (AtlasBoard_GetBno085Sample(board, SH2_ROTATION_VECTOR,
                                   &atlas_rtos.working_snapshot.bno_rotation_vector,
                                   true))
    {
        atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_BNO_ROTATION_VECTOR;
        atlas_rtos.working_snapshot.bno_rotation_vector_received_at_ms = now_ms;
    }
    if (AtlasGnss_GetLatestNavPvt(&board->gnss,
                                  &atlas_rtos.working_snapshot.gnss_nav_pvt,
                                  true))
    {
        atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_GNSS_NAV_PVT;
    }
    if (AtlasGnss_GetPps(&board->gnss, &atlas_rtos.working_snapshot.gnss_pps))
    {
        atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_GNSS_PPS;
    }
}

/**
 * @brief Build a validity-bit mask for required samples that are missing or stale.
 * @param snapshot Coherent sensor publication copied under the snapshot mutex.
 * @param now_ms Current HAL monotonic millisecond tick.
 * @return Zero while healthy (including startup grace), otherwise stale validity bits.
 */
static uint32_t atlas_rtos_stale_sensor_mask(const AtlasRtosSnapshot *snapshot,
                                              uint32_t now_ms)
{
    uint32_t stale_mask;

    if ((uint32_t)(now_ms - atlas_rtos.started_at_ms) <
        ATLAS_RTOS_SENSOR_STARTUP_GRACE_MS)
    {
        return 0U;
    }
    /* A bounded post-maintenance window lets UART/SHTP backlogs drain before
       freshness is judged again; sampling errors still fail immediately. */
    if (snapshot->sensor_recovery_active &&
        ((int32_t)(snapshot->sensor_recovery_until_ms - now_ms) > 0))
    {
        return 0U;
    }

    stale_mask = ATLAS_RTOS_REQUIRED_SENSOR_MASK & ~snapshot->valid_mask;
    if (!AtlasRtosPolicy_TimestampFresh(now_ms, snapshot->adxl375.timestamp_ms,
                                        ATLAS_RTOS_ADXL_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_ADXL375;
    }
    if (!AtlasRtosPolicy_TimestampFresh(now_ms, snapshot->lsm6dsv16b.timestamp_ms,
                                        ATLAS_RTOS_LSM_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_LSM6DSV16B;
    }
    if (!AtlasRtosPolicy_TimestampFresh(now_ms, snapshot->mmc5983ma.timestamp_ms,
                                        ATLAS_RTOS_MMC_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_MMC5983MA;
    }
    if (!AtlasRtosPolicy_TimestampFresh(now_ms, snapshot->ms5611.timestamp_ms,
                                        ATLAS_RTOS_MS5611_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_MS5611;
    }
    if (!AtlasRtosPolicy_TimestampFresh(
            now_ms, snapshot->bno_accelerometer_received_at_ms,
            ATLAS_RTOS_BNO_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_BNO_ACCELEROMETER;
    }
    if (!AtlasRtosPolicy_TimestampFresh(
            now_ms, snapshot->bno_gyroscope_received_at_ms,
            ATLAS_RTOS_BNO_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_BNO_GYROSCOPE;
    }
    if (!AtlasRtosPolicy_TimestampFresh(
            now_ms, snapshot->bno_magnetometer_received_at_ms,
            ATLAS_RTOS_BNO_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_BNO_MAGNETOMETER;
    }
    if (!AtlasRtosPolicy_TimestampFresh(
            now_ms, snapshot->bno_rotation_vector_received_at_ms,
            ATLAS_RTOS_BNO_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_BNO_ROTATION_VECTOR;
    }
    if (!AtlasRtosPolicy_TimestampFresh(now_ms,
                                        snapshot->gnss_nav_pvt.received_at_ms,
                                        ATLAS_RTOS_GNSS_MAX_AGE_MS))
    {
        stale_mask |= ATLAS_RTOS_VALID_GNSS_NAV_PVT;
    }
    return stale_mask & ATLAS_RTOS_REQUIRED_SENSOR_MASK;
}

/**
 * @brief Poll and read the direct high-g accelerometer when a sample is ready.
 */
static void atlas_rtos_sample_adxl375(void)
{
    bool ready = false;
    AtlasStatus status = AtlasAdxl375_DataReady(&atlas_rtos.board->adxl375, &ready);

    if ((status == ATLAS_OK) && ready)
    {
        status = AtlasAdxl375_ReadSample(&atlas_rtos.board->adxl375,
                                         &atlas_rtos.working_snapshot.adxl375);
        if (status == ATLAS_OK)
        {
            atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_ADXL375;
        }
    }
    atlas_rtos.working_snapshot.adxl375_status = status;
    atlas_rtos_record_sampling_failure(status);
}

/**
 * @brief Poll and read the direct low-g IMU when both data paths are ready.
 */
static void atlas_rtos_sample_lsm6dsv16b(void)
{
    bool ready = false;
    AtlasStatus status = AtlasLsm6dsv16b_DataReady(&atlas_rtos.board->lsm6dsv16b,
                                                    &ready);

    if ((status == ATLAS_OK) && ready)
    {
        status = AtlasLsm6dsv16b_ReadSample(&atlas_rtos.board->lsm6dsv16b,
                                            &atlas_rtos.working_snapshot.lsm6dsv16b);
        if (status == ATLAS_OK)
        {
            atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_LSM6DSV16B;
        }
    }
    atlas_rtos.working_snapshot.lsm6dsv16b_status = status;
    atlas_rtos_record_sampling_failure(status);
}

/**
 * @brief Trigger one bounded MMC5983MA magnetic conversion.
 */
static void atlas_rtos_sample_mmc5983ma(void)
{
    AtlasStatus status = AtlasMmc5983ma_ReadField(&atlas_rtos.board->mmc5983ma,
                                                  &atlas_rtos.working_snapshot.mmc5983ma,
                                                  10U);

    atlas_rtos.working_snapshot.mmc5983ma_status = status;
    if (status == ATLAS_OK)
    {
        atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_MMC5983MA;
    }
    atlas_rtos_record_sampling_failure(status);
}

/**
 * @brief Trigger one compensated MS5611 pressure/temperature pair at OSR 1024.
 */
static void atlas_rtos_sample_ms5611(void)
{
    AtlasStatus status = AtlasMs5611_Read(&atlas_rtos.board->ms5611,
                                          ATLAS_MS5611_OSR_1024,
                                          &atlas_rtos.working_snapshot.ms5611);

    atlas_rtos.working_snapshot.ms5611_status = status;
    if (status == ATLAS_OK)
    {
        atlas_rtos.working_snapshot.valid_mask |= ATLAS_RTOS_VALID_MS5611;
    }
    atlas_rtos_record_sampling_failure(status);
}

/**
 * @brief Drain one driver UART ring into a single-reader FreeRTOS stream buffer.
 * @param radio true for RFD900x, false for BLE.
 */
static void atlas_rtos_drain_stream(bool radio)
{
    uint8_t bytes[ATLAS_UART_RX_CHUNK_CAPACITY];
    size_t received;
    size_t written;
    StreamBufferHandle_t stream;

    if (radio)
    {
        if (atlas_rtos.board->radio.command_mode)
        {
            return;
        }
        received = AtlasRfd900x_Read(&atlas_rtos.board->radio, bytes, sizeof(bytes));
        stream = atlas_rtos.radio_rx_stream;
    }
    else
    {
        if (atlas_rtos.board->ble.command_mode)
        {
            return;
        }
        received = AtlasBle_ReadData(&atlas_rtos.board->ble, bytes, sizeof(bytes));
        stream = atlas_rtos.ble_rx_stream;
    }
    if (received == 0U)
    {
        return;
    }

    written = xStreamBufferSend(stream, bytes, received, 0U);
    if (written < received)
    {
        if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
        {
            if (radio)
            {
                atlas_rtos.health.radio_rx_dropped_bytes += (uint32_t)(received - written);
            }
            else
            {
                atlas_rtos.health.ble_rx_dropped_bytes += (uint32_t)(received - written);
            }
            xSemaphoreGive(atlas_rtos.health_mutex);
        }
    }
}

/**
 * @brief Declare a bounded long I/O operation so the supervisor uses its deadline.
 * @param duration_ms Maximum reviewed operation duration.
 * @return true when the operation may start; false during sensor recovery.
 */
static bool atlas_rtos_begin_long_io(uint32_t duration_ms)
{
    const uint32_t now_ms = HAL_GetTick();
    AtlasIoSnapshot outputs;

    if (atlas_rtos.working_snapshot.sensor_recovery_active &&
        ((int32_t)(atlas_rtos.working_snapshot.sensor_recovery_until_ms -
                   now_ms) > 0))
    {
        return false;
    }
    /* Do not silently interrupt armed/active outputs for a commissioning request.
     * Prevent a higher-priority output task enabling between this check and hold. */
    taskENTER_CRITICAL();
    const bool safe_for_maintenance = AtlasIo_GetSnapshot(&outputs) &&
        !outputs.pyro.software_armed && outputs.pwm_enabled_mask == 0U && outputs.gpio_commanded_high == 0U;
    if (safe_for_maintenance) atlas_maintenance_hold = true;
    taskEXIT_CRITICAL();
    if (!safe_for_maintenance) return false;
    if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
    {
        atlas_rtos.health.io_busy = true;
        atlas_rtos.health.io_busy_until_ms = now_ms + duration_ms;
        xSemaphoreGive(atlas_rtos.health_mutex);
    }
    atlas_rtos.working_snapshot.maintenance_active = true;
    atlas_maintenance_hold = true;
    atlas_outputs_permitted = false;
    atlas_rtos.working_snapshot.sensor_recovery_active = false;
    atlas_rtos.working_snapshot.sensor_recovery_until_ms = 0U;
    /* Publish the inhibit state before a mode-transition call can yield. */
    atlas_rtos_publish_snapshot(now_ms);
    return true;
}

/**
 * @brief Close a declared long operation and start bounded sensor recovery.
 */
static void atlas_rtos_end_long_io(void)
{
    uint32_t now_ms = HAL_GetTick();

    atlas_rtos.working_snapshot.maintenance_active = false;
    atlas_rtos.working_snapshot.sensor_recovery_active = true;
    atlas_rtos.working_snapshot.sensor_recovery_until_ms =
        now_ms + ATLAS_RTOS_SENSOR_RECOVERY_GRACE_MS;
    /* Publish recovery before dropping the busy gate. A supervisor preemption
       can therefore observe either valid maintenance or valid recovery state,
       never the stale pre-maintenance snapshot with io_busy already cleared. */
    atlas_rtos_publish_snapshot(now_ms);
    now_ms = HAL_GetTick();

    if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
    {
        /* Catch an overrun even if the task completed between supervisor ticks. */
        if (atlas_rtos.health.io_busy &&
            ((int32_t)(atlas_rtos.health.io_busy_until_ms - now_ms) <= 0) &&
            (atlas_rtos.health.fault == ATLAS_RTOS_FAULT_NONE))
        {
            atlas_rtos.health.fault = ATLAS_RTOS_FAULT_IO_DEADLINE;
            atlas_rtos.health.state = ATLAS_RTOS_STATE_FAULTED;
            AtlasRtos_InhibitOutputs();
        }
        /* Completing a long operation is legitimate I/O progress. Advance the
           heartbeat in the same critical section that removes the busy gate so
           the supervisor cannot diagnose an artificial handoff stall. */
        ++atlas_rtos.health.io_heartbeat;
        atlas_rtos.health.io_busy = false;
        atlas_rtos.health.io_busy_until_ms = 0U;
        ++atlas_rtos.health.sensor_recovery_windows;
        xSemaphoreGive(atlas_rtos.health_mutex);
    }
}

/**
 * @brief Expire a post-maintenance recovery marker using wrap-safe arithmetic.
 * @param now_ms Current HAL monotonic millisecond tick.
 */
static void atlas_rtos_update_sensor_recovery(uint32_t now_ms)
{
    if (atlas_rtos.working_snapshot.sensor_recovery_active &&
        ((int32_t)(atlas_rtos.working_snapshot.sensor_recovery_until_ms -
                   now_ms) <= 0))
    {
        atlas_rtos.working_snapshot.sensor_recovery_active = false;
        atlas_rtos.working_snapshot.sensor_recovery_until_ms = 0U;
        atlas_maintenance_hold = false;
    }
}

/**
 * @brief Return the reviewed watchdog deadline for one maintenance command.
 * @param type Command type.
 * @param duration_ms Destination duration when the command is long-running.
 * @return true only when the requested radio/BLE work needs a maintenance window.
 */
static bool atlas_rtos_long_command_deadline(AtlasRtosCommandType type,
                                              uint32_t *duration_ms)
{
    switch (type)
    {
        case ATLAS_RTOS_COMMAND_RADIO_READ_IDENTITY:
        case ATLAS_RTOS_COMMAND_RADIO_READ_SETTINGS:
        case ATLAS_RTOS_COMMAND_RADIO_HOST_BAUD:
            *duration_ms = ATLAS_RTOS_RADIO_QUERY_DEADLINE_MS;
            return true;
        case ATLAS_RTOS_COMMAND_RADIO_SET_PARAMETER:
            *duration_ms = ATLAS_RTOS_RADIO_PARAMETER_DEADLINE_MS;
            return true;
        case ATLAS_RTOS_COMMAND_BLE_CONFIGURE_SPS:
            *duration_ms = ATLAS_RTOS_BLE_PROFILE_DEADLINE_MS;
            return true;
        case ATLAS_RTOS_COMMAND_RADIO_ENTER_COMMAND_MODE:
            if (atlas_rtos.board->radio.command_mode)
            {
                *duration_ms = 0U;
                return false;
            }
            *duration_ms = ATLAS_RTOS_RADIO_MODE_DEADLINE_MS;
            return true;
        case ATLAS_RTOS_COMMAND_RADIO_EXIT_COMMAND_MODE:
            if (!atlas_rtos.board->radio.command_mode)
            {
                *duration_ms = 0U;
                return false;
            }
            *duration_ms = ATLAS_RTOS_RADIO_MODE_DEADLINE_MS;
            return true;
        case ATLAS_RTOS_COMMAND_BLE_ENTER_COMMAND_MODE:
            if (atlas_rtos.board->ble.command_mode)
            {
                *duration_ms = 0U;
                return false;
            }
            *duration_ms = ATLAS_RTOS_BLE_COMMAND_MODE_DEADLINE_MS;
            return true;
        case ATLAS_RTOS_COMMAND_BLE_ENTER_DATA_MODE:
            if (!atlas_rtos.board->ble.command_mode)
            {
                *duration_ms = 0U;
                return false;
            }
            *duration_ms = ATLAS_RTOS_BLE_DATA_MODE_DEADLINE_MS;
            return true;
        default:
            *duration_ms = 0U;
            return false;
    }
}

/**
 * @brief Execute one already validated command in the hardware-owner task.
 * @param command Queued command and ticket.
 * @return Driver execution result.
 */
static AtlasStatus atlas_rtos_execute_command(const AtlasRtosQueuedCommand *command)
{
    const AtlasRtosCommandRequest *request = &command->request;

    switch (request->type)
    {
        case ATLAS_RTOS_COMMAND_LED_SET:
            return AtlasLed_SetColor(&atlas_rtos.board->led,
                                     request->arguments.led_color);
        case ATLAS_RTOS_COMMAND_BUZZER_BEEP:
            return AtlasBuzzer_Beep(&atlas_rtos.board->buzzer,
                                    request->arguments.buzzer.frequency_hz,
                                    request->arguments.buzzer.duration_ms);
        case ATLAS_RTOS_COMMAND_BUZZER_STOP:
            AtlasBuzzer_Stop(&atlas_rtos.board->buzzer);
            return ATLAS_OK;
        case ATLAS_RTOS_COMMAND_RADIO_WRITE:
            return AtlasRfd900x_Write(&atlas_rtos.board->radio,
                                      request->arguments.stream.bytes,
                                      request->arguments.stream.length,
                                      request->arguments.stream.timeout_ms);
        case ATLAS_RTOS_COMMAND_BLE_WRITE:
            return AtlasBle_WriteData(&atlas_rtos.board->ble,
                                      request->arguments.stream.bytes,
                                      request->arguments.stream.length,
                                      request->arguments.stream.timeout_ms);
        case ATLAS_RTOS_COMMAND_RADIO_ENTER_COMMAND_MODE:
            return AtlasRfd900x_EnterCommandMode(&atlas_rtos.board->radio);
        case ATLAS_RTOS_COMMAND_RADIO_EXIT_COMMAND_MODE:
            return AtlasRfd900x_ExitCommandMode(&atlas_rtos.board->radio);
        case ATLAS_RTOS_COMMAND_BLE_ENTER_COMMAND_MODE:
            return AtlasBle_EnterCommandMode(&atlas_rtos.board->ble);
        case ATLAS_RTOS_COMMAND_BLE_ENTER_DATA_MODE:
            return AtlasBle_EnterDataMode(&atlas_rtos.board->ble);
        case ATLAS_RTOS_COMMAND_RADIO_READ_IDENTITY:
            return AtlasRfd900x_ReadIdentity(&atlas_rtos.board->radio, atlas_rtos.pending_reply.text,
                                             sizeof(atlas_rtos.pending_reply.text));
        case ATLAS_RTOS_COMMAND_RADIO_READ_SETTINGS:
            return AtlasRfd900x_ReadSettings(&atlas_rtos.board->radio, atlas_rtos.pending_reply.text,
                                             sizeof(atlas_rtos.pending_reply.text));
        case ATLAS_RTOS_COMMAND_RADIO_SET_PARAMETER:
            return AtlasRfd900x_SetParameter(&atlas_rtos.board->radio, request->arguments.radio_parameter.index,
                request->arguments.radio_parameter.value, request->arguments.radio_parameter.persist);
        case ATLAS_RTOS_COMMAND_RADIO_HOST_BAUD:
            return AtlasRfd900x_ReconfigureHostBaud(&atlas_rtos.board->radio, request->arguments.radio_host_baud);
        case ATLAS_RTOS_COMMAND_BLE_CONFIGURE_SPS:
            return AtlasBle_ConfigureSps(&atlas_rtos.board->ble, request->arguments.ble_profile.name,
                                         request->arguments.ble_profile.persist);
        default:
            return ATLAS_ERROR_ARGUMENT;
    }
}

/**
 * @brief Execute at most one queued command and publish its asynchronous result.
 * @param maintenance Receives true when a declared maintenance interval was used.
 * @return true when any command was consumed, including a rejected command.
 */
static bool atlas_rtos_service_command(bool *maintenance)
{
    AtlasRtosQueuedCommand command;
    AtlasStatus status;
    uint32_t long_io_deadline_ms;
    bool command_allowed = false;
    bool long_io_started = false;

    *maintenance = false;
    if (xQueuePeek(atlas_rtos.command_queue, &command, 0U) != pdTRUE)
    {
        return false;
    }
    const bool has_reply = command.request.type == ATLAS_RTOS_COMMAND_RADIO_READ_IDENTITY ||
                           command.request.type == ATLAS_RTOS_COMMAND_RADIO_READ_SETTINGS;
    if (has_reply && uxQueueSpacesAvailable(atlas_rtos.maintenance_replies) == 0U) return false;
    if (xQueueReceive(atlas_rtos.command_queue, &command, 0U) != pdTRUE) return false;
    memset(&atlas_rtos.pending_reply, 0, sizeof(atlas_rtos.pending_reply));
    if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
    {
        command_allowed =
            (atlas_rtos.health.state == ATLAS_RTOS_STATE_RUNNING) &&
            (atlas_rtos.health.fault == ATLAS_RTOS_FAULT_NONE) &&
            (atlas_rtos.health.startup_status == ATLAS_OK) &&
            (atlas_rtos.health.service_status == ATLAS_OK) &&
            (atlas_rtos.health.sampling_status == ATLAS_OK);
        xSemaphoreGive(atlas_rtos.health_mutex);
    }

    if (!command_allowed)
    {
        status = ATLAS_ERROR_STATE;
    }
    else if (atlas_rtos_long_command_deadline(command.request.type,
                                               &long_io_deadline_ms))
    {
        long_io_started = atlas_rtos_begin_long_io(long_io_deadline_ms);
        status = long_io_started ? atlas_rtos_execute_command(&command) :
                                   ATLAS_ERROR_BUSY;
        if (long_io_started)
        {
            atlas_rtos_end_long_io();
        }
    }
    else
    {
        status = atlas_rtos_execute_command(&command);
    }

    if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
    {
        atlas_rtos.health.last_command_ticket = command.ticket;
        atlas_rtos.health.last_command_type = command.request.type;
        atlas_rtos.health.last_command_status = status;
        ++atlas_rtos.health.commands_completed;
        if (status != ATLAS_OK)
        {
            ++atlas_rtos.health.command_failures;
        }
        xSemaphoreGive(atlas_rtos.health_mutex);
    }
    if (has_reply)
    {
        atlas_rtos.pending_reply.ticket = command.ticket;
        atlas_rtos.pending_reply.type = command.request.type;
        atlas_rtos.pending_reply.status = status;
        configASSERT(xQueueSend(atlas_rtos.maintenance_replies, &atlas_rtos.pending_reply, 0U) == pdTRUE);
    }
    /* Publish before notifying: the completion hook may immediately read it. */
    AtlasRtos_CommandCompleted(command.ticket, command.request.type, status);
    *maintenance = long_io_started;
    return true;
}

/**
 * @brief Own all post-start driver calls and construct coherent sensor publications.
 * @param argument Unused task argument.
 */
static void atlas_rtos_io_task(void *argument)
{
    uint32_t last_adxl_ms;
    uint32_t last_lsm_ms;
    uint32_t last_mmc_ms;
    uint32_t last_ms5611_ms;
    (void)argument;

    last_adxl_ms = HAL_GetTick() - ATLAS_RTOS_ADXL_PERIOD_MS;
    last_lsm_ms = HAL_GetTick() - ATLAS_RTOS_LSM_PERIOD_MS;
    last_mmc_ms = HAL_GetTick() - ATLAS_RTOS_MMC_PERIOD_MS;
    last_ms5611_ms = HAL_GetTick() - ATLAS_RTOS_MS5611_PERIOD_MS;

    for (;;)
    {
        const uint32_t cycle_start_ms = HAL_GetTick();
        bool slow_sample = false;
        bool maintenance_cycle = false;
        bool command_cycle = false;
        AtlasStatus service_status = AtlasBoard_Service(atlas_rtos.board);
        if (service_status != ATLAS_OK) AtlasRtos_InhibitOutputs();
        const bool lsm_interrupt =
            AtlasLsm6dsv16b_ConsumeInterrupt(&atlas_rtos.board->lsm6dsv16b);

        atlas_rtos_update_sensor_recovery(cycle_start_ms);
        atlas_rtos.working_snapshot.board_service_status = service_status;
        if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
        {
            atlas_rtos.health.service_status =
                atlas_rtos_accumulate(atlas_rtos.health.service_status, service_status);
            xSemaphoreGive(atlas_rtos.health_mutex);
        }

        atlas_rtos_capture_protocol_samples();
        if (AtlasRtosPolicy_PeriodDue(cycle_start_ms, last_adxl_ms,
                                      ATLAS_RTOS_ADXL_PERIOD_MS))
        {
            last_adxl_ms = cycle_start_ms;
            atlas_rtos_sample_adxl375();
        }
        if (lsm_interrupt ||
            AtlasRtosPolicy_PeriodDue(cycle_start_ms, last_lsm_ms,
                                      ATLAS_RTOS_LSM_PERIOD_MS))
        {
            last_lsm_ms = cycle_start_ms;
            atlas_rtos_sample_lsm6dsv16b();
        }
        if (AtlasRtosPolicy_PeriodDue(cycle_start_ms, last_mmc_ms,
                                      ATLAS_RTOS_MMC_PERIOD_MS))
        {
            last_mmc_ms = cycle_start_ms;
            atlas_rtos_sample_mmc5983ma();
            slow_sample = true;
        }
        else if (AtlasRtosPolicy_PeriodDue(cycle_start_ms, last_ms5611_ms,
                                      ATLAS_RTOS_MS5611_PERIOD_MS))
        {
            last_ms5611_ms = cycle_start_ms;
            atlas_rtos_sample_ms5611();
            slow_sample = true;
        }

        /* Avoid stacking two conversions and a bounded UART wait into one cycle. */
        if (!slow_sample) command_cycle = atlas_rtos_service_command(&maintenance_cycle);
        (void)AtlasExpansion_Service(!slow_sample && !command_cycle &&
            !atlas_output_fault && !atlas_maintenance_hold);
        atlas_rtos_drain_stream(true);
        atlas_rtos_drain_stream(false);
        atlas_rtos_publish_snapshot(HAL_GetTick());

        if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
        {
            ++atlas_rtos.health.io_heartbeat;
            if (!maintenance_cycle && (uint32_t)(HAL_GetTick() - cycle_start_ms) >=
                ATLAS_RTOS_IO_CYCLE_DEADLINE_MS)
            {
                ++atlas_rtos.health.io_deadline_misses;
                if (atlas_rtos.health.fault == ATLAS_RTOS_FAULT_NONE)
                {
                    atlas_rtos.health.fault = ATLAS_RTOS_FAULT_IO_DEADLINE;
                    atlas_rtos.health.state = ATLAS_RTOS_STATE_FAULTED;
                }
                AtlasRtos_InhibitOutputs();
            }
            xSemaphoreGive(atlas_rtos.health_mutex);
        }
        vTaskDelay(atlas_rtos_timeout_ticks(ATLAS_RTOS_IO_PERIOD_MS));
    }
}

/**
 * @brief Invoke the user-overridable control step at a fixed 100 Hz cadence.
 * @param argument Unused task argument.
 */
static void atlas_rtos_application_task(void *argument)
{
    TickType_t previous_wake = xTaskGetTickCount();
    const TickType_t period = atlas_rtos_timeout_ticks(ATLAS_RTOS_APPLICATION_PERIOD_MS);
    AtlasRtosSnapshot snapshot;
    (void)argument;
    _Static_assert(sizeof(TickType_t) == sizeof(uint32_t), "Deadline policy requires 32-bit ticks");

    for (;;)
    {
        vTaskDelayUntil(&previous_wake, period);
        const TickType_t released_at = previous_wake; /* Scheduled, not observed, release. */
        const uint32_t lateness = (uint32_t)(xTaskGetTickCount() - released_at);
        bool late = AtlasRtosPolicy_ResponseLate(xTaskGetTickCount(), released_at, period);
        bool copied = false;
        if (late) AtlasRtos_InhibitOutputs();
        if (!late) copied = AtlasRtos_GetSnapshot(&snapshot, ATLAS_RTOS_MAX_QUEUE_WAIT_MS);
        if (copied && AtlasRtos_OutputsPermitted() && !snapshot.maintenance_active &&
            !snapshot.sensor_recovery_active)
        {
            /* Snapshot-mutex wait also consumes the scheduled response budget. */
            if (!AtlasRtosPolicy_ResponseLate(xTaskGetTickCount(), released_at, period))
                AtlasRtos_ApplicationStep(&snapshot, HAL_GetTick());
        }
        late = late || AtlasRtosPolicy_ResponseLate(xTaskGetTickCount(), released_at, period);
        if (late) AtlasRtos_InhibitOutputs();
        if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
        {
            if (copied) ++atlas_rtos.health.application_heartbeat;
            if (lateness > atlas_rtos.health.maximum_application_lateness_ticks)
                atlas_rtos.health.maximum_application_lateness_ticks = lateness;
            if (late)
            {
                ++atlas_rtos.health.application_deadline_misses;
                ++atlas_rtos.health.application_resynchronizations;
                if (atlas_rtos.health.fault == ATLAS_RTOS_FAULT_NONE)
                {
                    atlas_rtos.health.fault = ATLAS_RTOS_FAULT_APPLICATION_DEADLINE;
                    atlas_rtos.health.state = ATLAS_RTOS_STATE_FAULTED;
                }
                /* Skip missed releases; never issue a burst of catch-up control steps. */
                previous_wake = xTaskGetTickCount();
            }
            xSemaphoreGive(atlas_rtos.health_mutex);
        }
    }
}

/**
 * @brief Supervise task progress and refresh IWDG only on a fully healthy decision.
 * @param argument Unused task argument.
 */
static void atlas_rtos_supervisor_task(void *argument)
{
    TickType_t previous_wake = xTaskGetTickCount();
    uint32_t previous_io_heartbeat = 0U;
    uint32_t previous_application_heartbeat = 0U;
    uint32_t previous_output_heartbeat = 0U;
    (void)argument;

    for (;;)
    {
        AtlasRtosSupervisorInput input;
        AtlasRtosSnapshot snapshot = {0};
        AtlasIoSnapshot output = {0};
        AtlasRtosFault fault;
        AtlasRtosFault latched_fault = ATLAS_RTOS_FAULT_NONE;
        uint32_t stale_sensor_mask = ATLAS_RTOS_REQUIRED_SENSOR_MASK;

        vTaskDelayUntil(&previous_wake,
                        atlas_rtos_timeout_ticks(ATLAS_RTOS_SUPERVISOR_PERIOD_MS));
        memset(&input, 0, sizeof(input));
        input.now_ms = HAL_GetTick();
        input.previous_io_heartbeat = previous_io_heartbeat;
        input.previous_application_heartbeat = previous_application_heartbeat;
        input.minimum_stack_free_words = ATLAS_RTOS_MIN_STACK_FREE_WORDS;
        input.io_stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(atlas_rtos.io_task);
        input.application_stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(atlas_rtos.application_task);
        input.supervisor_stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(atlas_rtos.supervisor_task);
        input.idle_stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandle());

        if (xSemaphoreTake(atlas_rtos.snapshot_mutex, portMAX_DELAY) == pdTRUE)
        {
            snapshot = atlas_rtos.published_snapshot;
            xSemaphoreGive(atlas_rtos.snapshot_mutex);
            stale_sensor_mask = atlas_rtos_stale_sensor_mask(&snapshot, input.now_ms);
        }
        input.sensors_fresh = (stale_sensor_mask == 0U);

        if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
        {
            input.startup_status = atlas_rtos.health.startup_status;
            input.service_status = atlas_rtos.health.service_status;
            input.sampling_status = atlas_rtos.health.sampling_status;
            input.io_heartbeat = atlas_rtos.health.io_heartbeat;
            input.application_heartbeat = atlas_rtos.health.application_heartbeat;
            input.io_busy = atlas_rtos.health.io_busy;
            input.io_busy_until_ms = atlas_rtos.health.io_busy_until_ms;
            latched_fault = atlas_rtos.health.fault;
            atlas_rtos.health.io_stack_free_words = input.io_stack_free_words;
            atlas_rtos.health.application_stack_free_words =
                input.application_stack_free_words;
            atlas_rtos.health.supervisor_stack_free_words =
                input.supervisor_stack_free_words;
            atlas_rtos.health.idle_stack_free_words = input.idle_stack_free_words;
            ++atlas_rtos.health.sensor_freshness_checks;
            ++atlas_rtos.health.supervisor_cycles;
            xSemaphoreGive(atlas_rtos.health_mutex);
        }

        const bool output_available = AtlasIo_GetSnapshot(&output);
        /* A higher-priority publisher may have run while these copies waited.
         * Age calculations must use a time sampled AFTER all publications. */
        input.now_ms = HAL_GetTick();
        stale_sensor_mask = atlas_rtos_stale_sensor_mask(&snapshot, input.now_ms);
        input.sensors_fresh = stale_sensor_mask == 0U;
        fault = (latched_fault == ATLAS_RTOS_FAULT_NONE) ?
                AtlasRtosPolicy_EvaluateSupervisor(&input) : latched_fault;
        if (fault == ATLAS_RTOS_FAULT_NONE &&
            (!output_available || output.status != ATLAS_OK || output.emergency_latched ||
             output.heartbeat == previous_output_heartbeat ||
             (uint32_t)(input.now_ms - output.published_at_ms) > 25U ||
             (uint32_t)(input.now_ms - output.analog.sampled_at_ms) > ATLAS_PYRO_MAX_SAMPLE_AGE_MS ||
             (output.analog.valid_mask & 1U) == 0U))
            fault = ATLAS_RTOS_FAULT_OUTPUT_SERVICE;
        if (fault == ATLAS_RTOS_FAULT_NONE && output.stack_free_words < ATLAS_RTOS_MIN_STACK_FREE_WORDS)
            fault = ATLAS_RTOS_FAULT_STACK_MARGIN;
        previous_output_heartbeat = output.heartbeat;
        if (fault == ATLAS_RTOS_FAULT_NONE)
        {
            if (HAL_IWDG_Refresh(atlas_rtos.watchdog) != HAL_OK)
            {
                fault = ATLAS_RTOS_FAULT_WATCHDOG;
            }
        }

        if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
        {
            if (fault == ATLAS_RTOS_FAULT_NONE)
            {
                ++atlas_rtos.health.watchdog_refreshes;
            }
            else if (atlas_rtos.health.fault == ATLAS_RTOS_FAULT_NONE)
            {
                atlas_rtos.health.fault = fault;
                if (fault == ATLAS_RTOS_FAULT_SENSOR_STALE)
                {
                    atlas_rtos.health.stale_sensor_mask = stale_sensor_mask;
                }
                atlas_rtos.health.state = ATLAS_RTOS_STATE_FAULTED;
            }
            xSemaphoreGive(atlas_rtos.health_mutex);
        }
        previous_io_heartbeat = input.io_heartbeat;
        previous_application_heartbeat = input.application_heartbeat;
        if (fault != ATLAS_RTOS_FAULT_NONE) AtlasRtos_InhibitOutputs();
        atlas_outputs_permitted = fault == ATLAS_RTOS_FAULT_NONE && !atlas_output_fault &&
            (uint32_t)(input.now_ms - atlas_rtos.started_at_ms) >= ATLAS_RTOS_SENSOR_STARTUP_GRACE_MS &&
            !atlas_maintenance_hold && !snapshot.maintenance_active &&
            !snapshot.sensor_recovery_active &&
            (snapshot.valid_mask & ATLAS_RTOS_REQUIRED_SENSOR_MASK) == ATLAS_RTOS_REQUIRED_SENSOR_MASK &&
            stale_sensor_mask == 0U;
    }
}

/**
 * @brief Validate a caller command without accessing hardware.
 * @param request Candidate request.
 * @return ATLAS_OK or a typed argument failure.
 */
static AtlasStatus atlas_rtos_validate_command(const AtlasRtosCommandRequest *request)
{
    if (request == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    switch (request->type)
    {
        case ATLAS_RTOS_COMMAND_LED_SET:
            if ((uint32_t)request->arguments.led_color > (uint32_t)ATLAS_LED_WHITE)
            {
                return ATLAS_ERROR_ARGUMENT;
            }
            return request->arguments.led_color == ATLAS_LED_OFF ?
                   ATLAS_OK : ATLAS_ERROR_UNSUPPORTED;
        case ATLAS_RTOS_COMMAND_BUZZER_BEEP:
            return ((request->arguments.buzzer.frequency_hz >=
                     ATLAS_BUZZER_MIN_FREQUENCY_HZ) &&
                    (request->arguments.buzzer.frequency_hz <=
                     ATLAS_BUZZER_MAX_FREQUENCY_HZ) &&
                    (request->arguments.buzzer.duration_ms != 0U) &&
                    (request->arguments.buzzer.duration_ms < UINT32_C(0x80000000))) ?
                   ATLAS_OK : ATLAS_ERROR_ARGUMENT;
        case ATLAS_RTOS_COMMAND_RADIO_WRITE:
        case ATLAS_RTOS_COMMAND_BLE_WRITE:
            return ((request->arguments.stream.length != 0U) &&
                    (request->arguments.stream.length <=
                     ATLAS_RTOS_COMMAND_PAYLOAD_CAPACITY) &&
                    (request->arguments.stream.timeout_ms != 0U) &&
                    (request->arguments.stream.timeout_ms <=
                     ATLAS_RTOS_MAX_STREAM_TX_TIMEOUT_MS)) ?
                   ATLAS_OK : ATLAS_ERROR_ARGUMENT;
        case ATLAS_RTOS_COMMAND_BUZZER_STOP:
        case ATLAS_RTOS_COMMAND_RADIO_READ_IDENTITY:
        case ATLAS_RTOS_COMMAND_RADIO_READ_SETTINGS:
        case ATLAS_RTOS_COMMAND_RADIO_SET_PARAMETER:
        case ATLAS_RTOS_COMMAND_RADIO_ENTER_COMMAND_MODE:
        case ATLAS_RTOS_COMMAND_RADIO_EXIT_COMMAND_MODE:
        case ATLAS_RTOS_COMMAND_BLE_ENTER_COMMAND_MODE:
        case ATLAS_RTOS_COMMAND_BLE_ENTER_DATA_MODE:
            return ATLAS_OK;
        case ATLAS_RTOS_COMMAND_RADIO_HOST_BAUD:
            return request->arguments.radio_host_baud >= 9600U && request->arguments.radio_host_baud <= 1000000U ?
                   ATLAS_OK : ATLAS_ERROR_ARGUMENT;
        case ATLAS_RTOS_COMMAND_BLE_CONFIGURE_SPS:
        {
            /* Match the driver's AT-string grammar before any strlen or I/O. */
            const char *name = request->arguments.ble_profile.name;
            size_t length = 0U;
            while (length < sizeof(request->arguments.ble_profile.name) && name[length] != '\0')
            {
                if (name[length] < 0x20 || name[length] > 0x7E || name[length] == '"' ||
                    name[length] == ',' || name[length] == '\\')
                    return ATLAS_ERROR_ARGUMENT;
                ++length;
            }
            return length > 0U && length < sizeof(request->arguments.ble_profile.name) ? ATLAS_OK : ATLAS_ERROR_ARGUMENT;
        }
        default:
            return ATLAS_ERROR_ARGUMENT;
    }
}

/**
 * @brief Create all static resources and start scheduling.
 * @param board Fully initialized board transferred to the I/O task.
 * @param watchdog Initialized independent watchdog.
 * @param startup_status Aggregate board initialization status.
 * @return Error only when startup cannot proceed or the scheduler returns.
 */
AtlasStatus AtlasRtos_Start(AtlasBoard *board,
                            IWDG_HandleTypeDef *watchdog,
                            AtlasStatus startup_status)
{
    if ((board == NULL) || (watchdog == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!board->init_complete ||
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
    {
        return ATLAS_ERROR_STATE;
    }
    if (HAL_NVIC_GetPriorityGrouping() != NVIC_PRIORITYGROUP_4)
    {
        return ATLAS_ERROR_STATE;
    }

    memset(&atlas_rtos, 0, sizeof(atlas_rtos));
    atlas_rtos.board = board;
    atlas_rtos.watchdog = watchdog;
    atlas_rtos.started_at_ms = HAL_GetTick();
    atlas_rtos.health.state = ATLAS_RTOS_STATE_STARTING;
    atlas_rtos.health.started_at_ms = atlas_rtos.started_at_ms;
    atlas_rtos.health.startup_status = startup_status;
    atlas_rtos.health.service_status = ATLAS_OK;
    atlas_rtos.health.sampling_status = ATLAS_OK;
    atlas_rtos.health.last_command_status = ATLAS_ERROR_NOT_READY;
    atlas_rtos.working_snapshot.board_service_status = ATLAS_OK;
    atlas_rtos.working_snapshot.adxl375_status = ATLAS_ERROR_NOT_READY;
    atlas_rtos.working_snapshot.lsm6dsv16b_status = ATLAS_ERROR_NOT_READY;
    atlas_rtos.working_snapshot.mmc5983ma_status = ATLAS_ERROR_NOT_READY;
    atlas_rtos.working_snapshot.ms5611_status = ATLAS_ERROR_NOT_READY;

    atlas_rtos.command_queue = xQueueCreateStatic(
        ATLAS_RTOS_COMMAND_QUEUE_CAPACITY,
        sizeof(AtlasRtosQueuedCommand),
        atlas_rtos.command_queue_storage,
        &atlas_rtos.command_queue_control);
    atlas_rtos.maintenance_replies = xQueueCreateStatic(4U, sizeof(AtlasRtosMaintenanceReply),
        atlas_rtos.maintenance_reply_memory, &atlas_rtos.maintenance_reply_control);
    atlas_rtos.snapshot_mutex =
        xSemaphoreCreateMutexStatic(&atlas_rtos.snapshot_mutex_control);
    atlas_rtos.health_mutex =
        xSemaphoreCreateMutexStatic(&atlas_rtos.health_mutex_control);
    atlas_rtos.radio_reader_mutex =
        xSemaphoreCreateMutexStatic(&atlas_rtos.radio_reader_mutex_control);
    atlas_rtos.ble_reader_mutex =
        xSemaphoreCreateMutexStatic(&atlas_rtos.ble_reader_mutex_control);
    atlas_rtos.radio_rx_stream = xStreamBufferCreateStatic(
        sizeof(atlas_rtos.radio_rx_storage), 1U,
        atlas_rtos.radio_rx_storage, &atlas_rtos.radio_rx_stream_control);
    atlas_rtos.ble_rx_stream = xStreamBufferCreateStatic(
        sizeof(atlas_rtos.ble_rx_storage), 1U,
        atlas_rtos.ble_rx_storage, &atlas_rtos.ble_rx_stream_control);

    if ((atlas_rtos.command_queue == NULL) || (atlas_rtos.maintenance_replies == NULL) ||
        (atlas_rtos.snapshot_mutex == NULL) ||
        (atlas_rtos.health_mutex == NULL) ||
        (atlas_rtos.radio_reader_mutex == NULL) ||
        (atlas_rtos.ble_reader_mutex == NULL) ||
        (atlas_rtos.radio_rx_stream == NULL) ||
        (atlas_rtos.ble_rx_stream == NULL))
    {
        return ATLAS_ERROR_STATE;
    }

    atlas_rtos.io_task = xTaskCreateStatic(
        atlas_rtos_io_task, "AtlasIO", ATLAS_RTOS_IO_STACK_WORDS, NULL,
        ATLAS_RTOS_IO_TASK_PRIORITY, atlas_rtos.io_stack,
        &atlas_rtos.io_task_control);
    atlas_rtos.application_task = xTaskCreateStatic(
        atlas_rtos_application_task, "AtlasControl",
        ATLAS_RTOS_APPLICATION_STACK_WORDS, NULL,
        ATLAS_RTOS_APPLICATION_TASK_PRIORITY, atlas_rtos.application_stack,
        &atlas_rtos.application_task_control);
    atlas_rtos.supervisor_task = xTaskCreateStatic(
        atlas_rtos_supervisor_task, "AtlasWatchdog",
        ATLAS_RTOS_SUPERVISOR_STACK_WORDS, NULL,
        ATLAS_RTOS_SUPERVISOR_TASK_PRIORITY, atlas_rtos.supervisor_stack,
        &atlas_rtos.supervisor_task_control);
    if ((atlas_rtos.io_task == NULL) ||
        (atlas_rtos.application_task == NULL) ||
        (atlas_rtos.supervisor_task == NULL))
    {
        return ATLAS_ERROR_STATE;
    }

    vQueueAddToRegistry(atlas_rtos.command_queue, "AtlasCommands");
    vQueueAddToRegistry(atlas_rtos.snapshot_mutex, "AtlasSnapshot");
    vQueueAddToRegistry(atlas_rtos.health_mutex, "AtlasHealth");
    vQueueAddToRegistry(atlas_rtos.radio_reader_mutex, "AtlasRadioRx");
    vQueueAddToRegistry(atlas_rtos.ble_reader_mutex, "AtlasBleRx");
    atlas_rtos.resources_ready = true;
    atlas_rtos.health.state = ATLAS_RTOS_STATE_RUNNING;

    if (AtlasStorage_Start(board->hardware.rtc) != ATLAS_OK ||
        AtlasUsb_Start() != ATLAS_OK || AtlasIo_Start(&board->hardware.io) != ATLAS_OK ||
        AtlasExpansion_Start(board->hardware.expansion_uart, board->hardware.expansion_i2c,
                             board->hardware.imu_spi) != ATLAS_OK)
    {
        atlas_rtos.resources_ready = false;
        return ATLAS_ERROR_STATE;
    }
    vTaskStartScheduler();

    atlas_rtos.health.state = ATLAS_RTOS_STATE_FAULTED;
    atlas_rtos.health.fault = ATLAS_RTOS_FAULT_SCHEDULER;
    atlas_rtos.resources_ready = false;
    return ATLAS_ERROR_STATE;
}

/**
 * @brief Copy the latest coherent sensor snapshot.
 * @param snapshot Destination snapshot.
 * @param timeout_ms Bounded mutex wait.
 * @return true when copied.
 */
bool AtlasRtos_GetSnapshot(AtlasRtosSnapshot *snapshot, uint32_t timeout_ms)
{
    if ((snapshot == NULL) || (timeout_ms > ATLAS_RTOS_MAX_QUEUE_WAIT_MS) ||
        !atlas_rtos_task_context())
    {
        return false;
    }
    if (xSemaphoreTake(atlas_rtos.snapshot_mutex,
                       atlas_rtos_timeout_ticks(timeout_ms)) != pdTRUE)
    {
        return false;
    }
    *snapshot = atlas_rtos.published_snapshot;
    xSemaphoreGive(atlas_rtos.snapshot_mutex);
    return true;
}

/** @brief Read a retained query reply. @param reply Destination. @return Availability. */
bool AtlasRtos_ReadMaintenanceReply(AtlasRtosMaintenanceReply *reply)
{
    return reply != NULL && atlas_rtos_task_context() &&
        xQueueReceive(atlas_rtos.maintenance_replies, reply, 0U) == pdTRUE;
}

/**
 * @brief Copy a coherent scheduler health record.
 * @param health Destination health record.
 * @param timeout_ms Bounded mutex wait.
 * @return true when copied.
 */
bool AtlasRtos_GetHealth(AtlasRtosHealth *health, uint32_t timeout_ms)
{
    if ((health == NULL) || (timeout_ms > ATLAS_RTOS_MAX_QUEUE_WAIT_MS) ||
        !atlas_rtos_task_context())
    {
        return false;
    }
    if (xSemaphoreTake(atlas_rtos.health_mutex,
                       atlas_rtos_timeout_ticks(timeout_ms)) != pdTRUE)
    {
        return false;
    }
    *health = atlas_rtos.health;
    xSemaphoreGive(atlas_rtos.health_mutex);
    return true;
}

/**
 * @brief Queue one validated hardware request.
 * @param request Caller request copied by value.
 * @param timeout_ms Bounded queue wait.
 * @param ticket Optional assigned ticket output.
 * @return ATLAS_OK on queue acceptance or a typed failure.
 */
AtlasStatus AtlasRtos_SubmitCommand(const AtlasRtosCommandRequest *request,
                                    uint32_t timeout_ms,
                                    uint32_t *ticket)
{
    AtlasRtosQueuedCommand command;
    AtlasStatus status = atlas_rtos_validate_command(request);

    if (status != ATLAS_OK)
    {
        return status;
    }
    if ((timeout_ms > ATLAS_RTOS_MAX_QUEUE_WAIT_MS) || !atlas_rtos_task_context())
    {
        return ATLAS_ERROR_STATE;
    }

    command.request = *request;
    taskENTER_CRITICAL();
    ++atlas_rtos.next_command_ticket;
    if (atlas_rtos.next_command_ticket == 0U)
    {
        ++atlas_rtos.next_command_ticket;
    }
    command.ticket = atlas_rtos.next_command_ticket;
    taskEXIT_CRITICAL();

    if (xQueueSend(atlas_rtos.command_queue, &command,
                   atlas_rtos_timeout_ticks(timeout_ms)) != pdTRUE)
    {
        if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
        {
            ++atlas_rtos.health.command_rejections;
            xSemaphoreGive(atlas_rtos.health_mutex);
        }
        return ATLAS_ERROR_BUSY;
    }
    if (xSemaphoreTake(atlas_rtos.health_mutex, portMAX_DELAY) == pdTRUE)
    {
        ++atlas_rtos.health.command_submissions;
        xSemaphoreGive(atlas_rtos.health_mutex);
    }
    if (ticket != NULL)
    {
        *ticket = command.ticket;
    }
    return ATLAS_OK;
}

/**
 * @brief Receive from one RTOS-owned single-reader byte stream.
 * @param stream Stream buffer handle.
 * @param reader_mutex Mutex serializing public readers.
 * @param destination Destination bytes.
 * @param capacity Destination capacity.
 * @param timeout_ms Bounded receive wait.
 * @return Number of bytes copied.
 */
static size_t atlas_rtos_read_stream(StreamBufferHandle_t stream,
                                     SemaphoreHandle_t reader_mutex,
                                     uint8_t *destination,
                                     size_t capacity,
                                     uint32_t timeout_ms)
{
    size_t count;

    if ((destination == NULL) || (capacity == 0U) ||
        (timeout_ms > ATLAS_RTOS_MAX_QUEUE_WAIT_MS) ||
        !atlas_rtos_task_context())
    {
        return 0U;
    }
    /* Serialize readers without spending the data wait budget twice. */
    if (xSemaphoreTake(reader_mutex, 0U) != pdTRUE)
    {
        return 0U;
    }
    count = xStreamBufferReceive(stream, destination, capacity,
                                 atlas_rtos_timeout_ticks(timeout_ms));
    xSemaphoreGive(reader_mutex);
    return count;
}

/**
 * @brief Read transparent RFD900x bytes from the RTOS stream.
 * @param destination Destination bytes.
 * @param capacity Destination capacity.
 * @param timeout_ms Bounded wait.
 * @return Number of bytes copied.
 */
size_t AtlasRtos_ReadRadio(uint8_t *destination,
                           size_t capacity,
                           uint32_t timeout_ms)
{
    return atlas_rtos_read_stream(atlas_rtos.radio_rx_stream,
                                  atlas_rtos.radio_reader_mutex,
                                  destination, capacity, timeout_ms);
}

/**
 * @brief Read transparent NINA-B112 bytes from the RTOS stream.
 * @param destination Destination bytes.
 * @param capacity Destination capacity.
 * @param timeout_ms Bounded wait.
 * @return Number of bytes copied.
 */
size_t AtlasRtos_ReadBle(uint8_t *destination,
                         size_t capacity,
                         uint32_t timeout_ms)
{
    return atlas_rtos_read_stream(atlas_rtos.ble_rx_stream,
                                  atlas_rtos.ble_reader_mutex,
                                  destination, capacity, timeout_ms);
}

/**
 * @brief Report whether Atlas scheduling is active.
 * @return true while running.
 */
bool AtlasRtos_IsRunning(void)
{
    return atlas_rtos_task_context();
}

/**
 * @brief Return a stable name for an RTOS fault.
 * @param fault Fault value.
 * @return Constant diagnostic string.
 */
const char *AtlasRtos_FaultName(AtlasRtosFault fault)
{
    switch (fault)
    {
        case ATLAS_RTOS_FAULT_NONE:                return "NONE";
        case ATLAS_RTOS_FAULT_STARTUP:             return "STARTUP";
        case ATLAS_RTOS_FAULT_BOARD_SERVICE:       return "BOARD_SERVICE";
        case ATLAS_RTOS_FAULT_SENSOR_SAMPLE:       return "SENSOR_SAMPLE";
        case ATLAS_RTOS_FAULT_SENSOR_STALE:        return "SENSOR_STALE";
        case ATLAS_RTOS_FAULT_IO_STALLED:          return "IO_STALLED";
        case ATLAS_RTOS_FAULT_APPLICATION_STALLED: return "APPLICATION_STALLED";
        case ATLAS_RTOS_FAULT_APPLICATION_DEADLINE: return "APPLICATION_DEADLINE";
        case ATLAS_RTOS_FAULT_IO_DEADLINE:         return "IO_DEADLINE";
        case ATLAS_RTOS_FAULT_STACK_MARGIN:        return "STACK_MARGIN";
        case ATLAS_RTOS_FAULT_WATCHDOG:            return "WATCHDOG";
        case ATLAS_RTOS_FAULT_ASSERT:              return "ASSERT";
        case ATLAS_RTOS_FAULT_STACK_OVERFLOW:      return "STACK_OVERFLOW";
        case ATLAS_RTOS_FAULT_SCHEDULER:           return "SCHEDULER";
        case ATLAS_RTOS_FAULT_OUTPUT_SERVICE:      return "OUTPUT_SERVICE";
        default:                                   return "UNKNOWN";
    }
}

/**
 * @brief Default no-op application integration hook.
 * @param snapshot Current coherent snapshot.
 * @param now_ms Current millisecond tick.
 */
__weak void AtlasRtos_ApplicationStep(const AtlasRtosSnapshot *snapshot,
                                      uint32_t now_ms)
{
    (void)snapshot;
    (void)now_ms;
}

/**
 * @brief Default no-op command completion hook.
 * @param ticket Completed command ticket.
 * @param type Completed command type.
 * @param status Execution result.
 */
__weak void AtlasRtos_CommandCompleted(uint32_t ticket,
                                       AtlasRtosCommandType type,
                                       AtlasStatus status)
{
    (void)ticket;
    (void)type;
    (void)status;
}

/**
 * @brief Supply statically allocated idle-task storage to FreeRTOS.
 * @param idle_task_tcb Receives the idle task control block.
 * @param idle_task_stack Receives the idle task stack.
 * @param idle_task_stack_size Receives the stack depth in StackType_t words.
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **idle_task_tcb,
                                   StackType_t **idle_task_stack,
                                   uint32_t *idle_task_stack_size)
{
    configASSERT(idle_task_tcb != NULL);
    configASSERT(idle_task_stack != NULL);
    configASSERT(idle_task_stack_size != NULL);
    *idle_task_tcb = &atlas_rtos_idle_task_control;
    *idle_task_stack = atlas_rtos_idle_stack;
    *idle_task_stack_size = ATLAS_RTOS_IDLE_STACK_WORDS;
}

/**
 * @brief Convert a detected task stack overflow into a watchdog-resetting fault.
 * @param task Task handle reported by FreeRTOS.
 * @param task_name Task name reported by FreeRTOS.
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    /* Prevent the supervisor from refreshing IWDG during partial fault capture. */
    __disable_irq();
    AtlasRtos_InhibitOutputs();
    atlas_rtos.health.assert_file = task_name;
    atlas_rtos.health.fault = ATLAS_RTOS_FAULT_STACK_OVERFLOW;
    atlas_rtos.health.state = ATLAS_RTOS_STATE_FAULTED;
    for (;;)
    {
        __NOP();
    }
}

/**
 * @brief Record assertion context and wait for the independent watchdog reset.
 * @param file Static source-file string.
 * @param line Source line.
 */
void AtlasRtos_AssertFailed(const char *file, uint32_t line)
{
    /* Prevent the supervisor from refreshing IWDG during partial fault capture. */
    __disable_irq();
    AtlasRtos_InhibitOutputs();
    atlas_rtos.health.assert_file = file;
    atlas_rtos.health.assert_line = line;
    atlas_rtos.health.fault = ATLAS_RTOS_FAULT_ASSERT;
    atlas_rtos.health.state = ATLAS_RTOS_STATE_FAULTED;
    for (;;)
    {
        __NOP();
    }
}
