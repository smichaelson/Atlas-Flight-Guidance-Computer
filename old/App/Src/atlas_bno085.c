/**
 * @file atlas_bno085.c
 * @brief Blocking, bounded I2C HAL for the official CEVA BNO085 SH-2 stack.
 *
 * Major functions:
 * - AtlasBno085_Init(): proves communication by retrieving SH-2 product IDs.
 * - AtlasBno085_EnableReport(): configures report interval and optional batching.
 * - AtlasBno085_Service(): dispatches decoded samples outside interrupt context.
 * - atlas_bno085_hal_read()/write(): implement SHTP-over-I2C transfer semantics.
 */

#include "atlas_bno085.h"

#include "atlas_time.h"

#include <stddef.h>
#include <string.h>

#include "sh2_err.h"
#include "sh2_hal.h"

#define BNO085_BOOT_TIMEOUT_MS      (4000U)
#define BNO085_RESET_ASSERT_MS      (10U)
#define BNO085_I2C_RETRY_COUNT      (3U)
#define BNO085_SHTP_HEADER_LENGTH   (4U)
#define BNO085_I2C_WIRE_HZ          (100000U)
#define BNO085_I2C_TIMEOUT_MARGIN_MS (50U)

/**
 * @brief Recover the enclosing Atlas instance from its first-member SH-2 HAL.
 * @param hal SH-2 HAL passed by the CEVA library.
 * @return Atlas BNO085 instance.
 */
static AtlasBno085 *atlas_bno085_from_hal(sh2_Hal_t *hal)
{
    return (AtlasBno085 *)hal;
}

/**
 * @brief Bound one blocking transfer for the generated 100 kHz I2C1 timing.
 * @param length Transfer bytes, excluding the address byte.
 * @return Wire time rounded up to milliseconds plus a clock-stretch margin.
 */
static uint32_t atlas_bno085_timeout_ms(uint16_t length)
{
    const uint32_t wire_bits = ((uint32_t)length + 1U) * 9U;
    const uint32_t wire_ms =
        (wire_bits * 1000U + BNO085_I2C_WIRE_HZ - 1U) / BNO085_I2C_WIRE_HZ;
    return wire_ms + BNO085_I2C_TIMEOUT_MARGIN_MS;
}

/**
 * @brief Retain enough HAL evidence to diagnose a failed field transaction.
 * @param sensor Driver instance.
 * @param status Terminal STM32 HAL result.
 * @param stage Operation that failed.
 * @param length Requested byte count.
 */
static void atlas_bno085_record_hal_failure(AtlasBno085 *sensor,
                                             HAL_StatusTypeDef status,
                                             AtlasBno085FailureStage stage,
                                             uint16_t length)
{
    ++sensor->health.io_errors;
    sensor->health.last_hal_status = (uint32_t)status;
    sensor->health.last_hal_error = HAL_I2C_GetError(sensor->i2c);
    sensor->health.last_failure_stage = (uint32_t)stage;
    sensor->health.last_transfer_length = length;
    sensor->transport_failed = true;
}

/**
 * @brief Perform a bounded I2C receive with retries for transient BNO085 NACKs.
 * @param sensor Driver instance.
 * @param data Destination buffer.
 * @param length Exact byte count.
 * @param stage Header or full-transfer diagnostic stage.
 * @return HAL_OK on success or the last HAL status.
 */
static HAL_StatusTypeDef atlas_bno085_receive(AtlasBno085 *sensor,
                                              uint8_t *data,
                                              uint16_t length,
                                              AtlasBno085FailureStage stage)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    uint32_t attempt;
    const uint32_t timeout_ms = atlas_bno085_timeout_ms(length);

    for (attempt = 0U; attempt < BNO085_I2C_RETRY_COUNT; ++attempt)
    {
        status = HAL_I2C_Master_Receive(sensor->i2c,
                                        sensor->address_hal,
                                        data,
                                        length,
                                        timeout_ms);
        if (status == HAL_OK)
        {
            return HAL_OK;
        }
        /* The hub may briefly NACK while changing SHTP channel state. */
        if (attempt + 1U < BNO085_I2C_RETRY_COUNT)
        {
            AtlasTime_DelayMs(1U);
        }
    }
    atlas_bno085_record_hal_failure(sensor, status, stage, length);
    return status;
}

/**
 * @brief Perform a bounded I2C transmit with retries for transient BNO085 NACKs.
 * @param sensor Driver instance.
 * @param data Source buffer.
 * @param length Exact byte count.
 * @return HAL_OK on success or the last HAL status.
 */
static HAL_StatusTypeDef atlas_bno085_transmit(AtlasBno085 *sensor,
                                               uint8_t *data,
                                               uint16_t length)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    uint32_t attempt;
    const uint32_t timeout_ms = atlas_bno085_timeout_ms(length);

    for (attempt = 0U; attempt < BNO085_I2C_RETRY_COUNT; ++attempt)
    {
        /* CEVA's reference I2C HAL inserts a short guard before every write. */
        AtlasTime_DelayMs(1U);
        status = HAL_I2C_Master_Transmit(sensor->i2c,
                                         sensor->address_hal,
                                         data,
                                         length,
                                         timeout_ms);
        if (status == HAL_OK)
        {
            return HAL_OK;
        }
    }
    atlas_bno085_record_hal_failure(sensor, status,
                                    ATLAS_BNO085_FAILURE_WRITE_TRANSFER, length);
    return status;
}

/**
 * @brief Determine whether a new H_INTN assertion authorizes one I2C read phase.
 * @param sensor Driver instance.
 * @return true after a new EXTI edge or a physically observed HIGH-to-LOW cycle.
 * @note A LOW level alone is insufficient because it may be the tail of the
 *       assertion that authorized the preceding four-byte header transaction.
 */
static bool atlas_bno085_read_phase_ready(AtlasBno085 *sensor)
{
    if (sensor->interrupt_pending)
    {
        return true;
    }
    if (HAL_GPIO_ReadPin(sensor->interrupt_port, sensor->interrupt_pin) == GPIO_PIN_SET)
    {
        sensor->interrupt_rearmed = true;
        return false;
    }
    if (!sensor->interrupt_rearmed)
    {
        return false;
    }

    /* Falling-edge polling fallback: a HIGH was observed since the last read,
     * but the EXTI callback was missed or has not yet run. */
    sensor->interrupt_timestamp_us =
        __HAL_TIM_GET_COUNTER(sensor->microsecond_timer);
    sensor->interrupt_rearmed = false;
    sensor->interrupt_pending = true;
    return true;
}

/**
 * @brief Rearm level-based recovery only after H_INTN is visibly deasserted.
 * @param sensor Driver instance.
 * @note A new EXTI edge that arrived during the blocking read remains pending.
 */
static void atlas_bno085_note_interrupt_deassertion(AtlasBno085 *sensor)
{
    if (HAL_GPIO_ReadPin(sensor->interrupt_port, sensor->interrupt_pin) == GPIO_PIN_SET)
    {
        sensor->interrupt_rearmed = true;
    }
}

/**
 * @brief Restore I2C1 after a terminal BNO transaction while U12 is held reset.
 * @param sensor Driver instance sharing I2C1 with the MS5611.
 * @return true only when deinit, init, and both generated filter settings succeed.
 */
static bool atlas_bno085_recover_shared_i2c(AtlasBno085 *sensor)
{
    bool recovered = true;
    HAL_StatusTypeDef status;

    ++sensor->health.bus_recovery_attempts;
    status = HAL_I2C_DeInit(sensor->i2c);
    if (status != HAL_OK)
    {
        recovered = false;
        sensor->health.last_hal_status = (uint32_t)status;
        sensor->health.last_hal_error = HAL_I2C_GetError(sensor->i2c);
        sensor->health.last_failure_stage = ATLAS_BNO085_FAILURE_I2C_DEINIT;
    }
    status = HAL_I2C_Init(sensor->i2c);
    if (status != HAL_OK)
    {
        recovered = false;
        sensor->health.last_hal_status = (uint32_t)status;
        sensor->health.last_hal_error = HAL_I2C_GetError(sensor->i2c);
        sensor->health.last_failure_stage = ATLAS_BNO085_FAILURE_I2C_INIT;
    }
    else
    {
        status = HAL_I2CEx_ConfigAnalogFilter(sensor->i2c, I2C_ANALOGFILTER_ENABLE);
        if (status != HAL_OK)
        {
            recovered = false;
            sensor->health.last_hal_status = (uint32_t)status;
            sensor->health.last_hal_error = HAL_I2C_GetError(sensor->i2c);
            sensor->health.last_failure_stage = ATLAS_BNO085_FAILURE_ANALOG_FILTER;
        }
        status = HAL_I2CEx_ConfigDigitalFilter(sensor->i2c, 0U);
        if (status != HAL_OK)
        {
            recovered = false;
            sensor->health.last_hal_status = (uint32_t)status;
            sensor->health.last_hal_error = HAL_I2C_GetError(sensor->i2c);
            sensor->health.last_failure_stage = ATLAS_BNO085_FAILURE_DIGITAL_FILTER;
        }
    }
    if (!recovered)
    {
        ++sensor->health.bus_recovery_failures;
    }
    return recovered;
}

/**
 * @brief Reset the sensor hub and wait for its active-low interrupt assertion.
 * @param hal SH-2 HAL supplied by the CEVA stack.
 * @return SH2_OK or SH2_ERR_TIMEOUT.
 */
static int atlas_bno085_hal_open(sh2_Hal_t *hal)
{
    AtlasBno085 *sensor = atlas_bno085_from_hal(hal);
    const uint32_t started_ms = HAL_GetTick();

    HAL_GPIO_WritePin(sensor->reset_port, sensor->reset_pin, GPIO_PIN_RESET);
    AtlasTime_DelayMs(BNO085_RESET_ASSERT_MS);
    sensor->interrupt_pending = false;
    sensor->interrupt_rearmed = false;
    sensor->pending_transfer_length = 0U;
    sensor->pending_timestamp_us = 0U;
    sensor->transport_failed = false;
    HAL_GPIO_WritePin(sensor->reset_port, sensor->reset_pin, GPIO_PIN_SET);

    while (HAL_GPIO_ReadPin(sensor->interrupt_port, sensor->interrupt_pin) != GPIO_PIN_RESET)
    {
        if ((HAL_GetTick() - started_ms) >= BNO085_BOOT_TIMEOUT_MS)
        {
            return SH2_ERR_TIMEOUT;
        }
        AtlasTime_DelayMs(1U);
    }
    sensor->interrupt_timestamp_us =
        __HAL_TIM_GET_COUNTER(sensor->microsecond_timer);
    sensor->interrupt_pending = true;
    sensor->interrupt_rearmed = false;
    return SH2_OK;
}

/**
 * @brief Hold the BNO085 in reset when SH-2 closes.
 * @param hal SH-2 HAL supplied by the CEVA stack.
 */
static void atlas_bno085_hal_close(sh2_Hal_t *hal)
{
    AtlasBno085 *sensor = atlas_bno085_from_hal(hal);
    HAL_GPIO_WritePin(sensor->reset_port, sensor->reset_pin, GPIO_PIN_RESET);
    sensor->interrupt_pending = false;
    sensor->interrupt_rearmed = false;
    sensor->pending_transfer_length = 0U;
    sensor->pending_timestamp_us = 0U;
    sensor->session_open = false;

    /* A timed-out/failed blocking transfer can leave the STM32 I2C state machine
     * unusable by the MS5611. Resetting U12 first releases any stretched clock. */
    if (sensor->transport_failed)
    {
        AtlasTime_DelayMs(BNO085_RESET_ASSERT_MS);
        (void)atlas_bno085_recover_shared_i2c(sensor);
    }
}

/**
 * @brief Deliver one phase of CEVA's two-interrupt SHTP-over-I2C receive contract.
 * @param hal SH-2 HAL supplied by the CEVA stack.
 * @param buffer Destination SHTP transfer buffer.
 * @param capacity Destination capacity.
 * @param timestamp_us Destination interrupt/read timestamp.
 * @return Four-byte length fragment, complete continuation transfer, zero if not ready,
 *         or a negative SH-2 error.
 */
static int atlas_bno085_hal_read(sh2_Hal_t *hal,
                                 uint8_t *buffer,
                                 unsigned capacity,
                                 uint32_t *timestamp_us)
{
    AtlasBno085 *sensor = atlas_bno085_from_hal(hal);
    uint8_t header[BNO085_SHTP_HEADER_LENGTH] = {0U};
    uint16_t transfer_length;
    uint32_t phase_timestamp_us;

    if ((buffer == NULL) || (timestamp_us == NULL) ||
        (capacity < BNO085_SHTP_HEADER_LENGTH))
    {
        return SH2_ERR_BAD_PARAM;
    }
    if (sensor->transport_failed)
    {
        return SH2_ERR_IO;
    }
    if (!atlas_bno085_read_phase_ready(sensor))
    {
        return 0;
    }

    phase_timestamp_us = sensor->interrupt_pending ?
                         sensor->interrupt_timestamp_us :
                         __HAL_TIM_GET_COUNTER(sensor->microsecond_timer);
    sensor->interrupt_pending = false;
    sensor->interrupt_rearmed = false;

    if (sensor->pending_transfer_length == 0U)
    {
        *timestamp_us = phase_timestamp_us;
        if (atlas_bno085_receive(sensor, header, sizeof(header),
                                 ATLAS_BNO085_FAILURE_READ_HEADER) != HAL_OK)
        {
            return SH2_ERR_IO;
        }

        transfer_length = (uint16_t)(((uint16_t)header[1] << 8) | header[0]);
        transfer_length &= 0x7FFFU; /* Bit 15 marks SHTP continuation, not length. */
        if (transfer_length == 0U)
        {
            atlas_bno085_note_interrupt_deassertion(sensor);
            return 0;
        }
        if ((transfer_length < BNO085_SHTP_HEADER_LENGTH) ||
            ((unsigned)transfer_length > capacity))
        {
            ++sensor->health.protocol_errors;
            sensor->health.last_failure_stage = ATLAS_BNO085_FAILURE_HEADER_LENGTH;
            sensor->health.last_transfer_length = transfer_length;
            atlas_bno085_note_interrupt_deassertion(sensor);
            return SH2_ERR_BAD_PARAM;
        }

        /* Per CEVA's reference HAL, SHTP first receives the four-byte fragment.
         * U12 then raises H_INTN again before the full continuation is read. */
        memcpy(buffer, header, sizeof(header));
        sensor->pending_transfer_length = transfer_length;
        sensor->pending_timestamp_us = *timestamp_us;
        atlas_bno085_note_interrupt_deassertion(sensor);
        return (int)BNO085_SHTP_HEADER_LENGTH;
    }

    transfer_length = sensor->pending_transfer_length;
    *timestamp_us = sensor->pending_timestamp_us;
    if ((unsigned)transfer_length > capacity)
    {
        ++sensor->health.protocol_errors;
        sensor->health.last_failure_stage = ATLAS_BNO085_FAILURE_TRANSFER_LENGTH;
        sensor->health.last_transfer_length = transfer_length;
        sensor->pending_transfer_length = 0U;
        return SH2_ERR_BAD_PARAM;
    }
    if (atlas_bno085_receive(sensor, buffer, transfer_length,
                             ATLAS_BNO085_FAILURE_READ_TRANSFER) != HAL_OK)
    {
        sensor->pending_transfer_length = 0U;
        return SH2_ERR_IO;
    }
    sensor->pending_transfer_length = 0U;
    sensor->pending_timestamp_us = 0U;
    if (((((uint16_t)buffer[1] << 8) | buffer[0]) & 0x7FFFU) != transfer_length)
    {
        ++sensor->health.protocol_errors;
        sensor->health.last_failure_stage = ATLAS_BNO085_FAILURE_TRANSFER_LENGTH;
        sensor->health.last_transfer_length = transfer_length;
        atlas_bno085_note_interrupt_deassertion(sensor);
        return SH2_ERR_IO;
    }

    ++sensor->health.transfers_read;
    sensor->health.last_transfer_length = transfer_length;
    /* A low level may still belong to this transaction. Only a new EXTI edge or
     * a later observed HIGH-to-LOW cycle can authorize another header read. */
    atlas_bno085_note_interrupt_deassertion(sensor);
    return (int)transfer_length;
}

/**
 * @brief Write one complete SHTP transfer to the BNO085.
 * @param hal SH-2 HAL supplied by the CEVA stack.
 * @param buffer SHTP transfer bytes.
 * @param length Transfer byte count.
 * @return Accepted byte count or a negative SH-2 error.
 */
static int atlas_bno085_hal_write(sh2_Hal_t *hal,
                                  uint8_t *buffer,
                                  unsigned length)
{
    AtlasBno085 *sensor = atlas_bno085_from_hal(hal);

    if ((buffer == NULL) || (length == 0U) ||
        (length > SH2_HAL_MAX_TRANSFER_OUT) || (length > UINT16_MAX))
    {
        return SH2_ERR_BAD_PARAM;
    }
    if (sensor->transport_failed)
    {
        return SH2_ERR_IO;
    }
    if (atlas_bno085_transmit(sensor, buffer, (uint16_t)length) != HAL_OK)
    {
        return SH2_ERR_IO;
    }
    ++sensor->health.transfers_written;
    sensor->health.last_transfer_length = (uint32_t)length;
    return (int)length;
}

/**
 * @brief Return the free-running 32-bit microsecond counter used by SH-2.
 * @param hal SH-2 HAL supplied by the CEVA stack.
 * @return Current timer count; wraparound is permitted by the SH-2 API.
 */
static uint32_t atlas_bno085_hal_time_us(sh2_Hal_t *hal)
{
    AtlasBno085 *sensor = atlas_bno085_from_hal(hal);
    return __HAL_TIM_GET_COUNTER(sensor->microsecond_timer);
}

/**
 * @brief Track asynchronous SH-2 reset notifications.
 * @param context Atlas BNO085 instance.
 * @param event SH-2 asynchronous event.
 */
static void atlas_bno085_async_callback(void *context, sh2_AsyncEvent_t *event)
{
    AtlasBno085 *sensor = (AtlasBno085 *)context;
    if ((sensor != NULL) && (event != NULL) && (event->eventId == SH2_RESET))
    {
        ++sensor->health.async_resets;
    }
}

/**
 * @brief Decode raw SH-2 events before invoking the application callback.
 * @param context Atlas BNO085 instance.
 * @param event Raw SH-2 sensor event.
 */
static void atlas_bno085_sensor_callback(void *context, sh2_SensorEvent_t *event)
{
    AtlasBno085 *sensor = (AtlasBno085 *)context;
    sh2_SensorValue_t decoded;

    if ((sensor == NULL) || (event == NULL))
    {
        return;
    }
    if (sh2_decodeSensorEvent(&decoded, event) != SH2_OK)
    {
        ++sensor->health.decode_errors;
        return;
    }

    ++sensor->health.decoded_samples;
    if (sensor->sample_callback != NULL)
    {
        sensor->sample_callback(sensor->sample_context, &decoded);
    }
}

/**
 * @brief Convert a CEVA SH-2 status to the common Atlas status type.
 * @param sh2_status CEVA result code.
 * @return Corresponding Atlas status.
 */
static AtlasStatus atlas_bno085_from_sh2(int sh2_status)
{
    if (sh2_status == SH2_OK)
    {
        return ATLAS_OK;
    }
    if (sh2_status == SH2_ERR_BAD_PARAM)
    {
        return ATLAS_ERROR_ARGUMENT;
    }
    if (sh2_status == SH2_ERR_TIMEOUT)
    {
        return ATLAS_ERROR_TIMEOUT;
    }
    if (sh2_status == SH2_ERR_OP_IN_PROGRESS)
    {
        return ATLAS_ERROR_BUSY;
    }
    return ATLAS_ERROR_IO;
}

/**
 * @brief Close an active singleton session, hold U12 reset, and recover a failed bus.
 * @param sensor Driver instance.
 * @note Safe during startup before sensor->initialized becomes true.
 */
static void atlas_bno085_abort_session(AtlasBno085 *sensor)
{
    if (sensor->session_open)
    {
        sh2_close();
    }
    else
    {
        atlas_bno085_hal_close(&sensor->hal);
    }
    sensor->initialized = false;
}

/**
 * @brief Reset the BNO085, open the official CEVA SH-2 stack, and read product IDs.
 * @param sensor Destination driver instance.
 * @param i2c Initialized I2C1 handle at no more than 400 kHz.
 * @param microsecond_timer Free-running 1 MHz TIM2 handle.
 * @param sample_callback Optional callback for decoded sensor samples.
 * @param sample_context Opaque value passed to sample_callback.
 * @return ATLAS_OK or a typed parameter, transport, timeout, or identity failure.
 */
AtlasStatus AtlasBno085_Init(AtlasBno085 *sensor,
                             I2C_HandleTypeDef *i2c,
                             TIM_HandleTypeDef *microsecond_timer,
                             AtlasBno085SampleCallback sample_callback,
                             void *sample_context)
{
    int sh2_status;
    AtlasStatus clock_status;

    if ((sensor == NULL) || (i2c == NULL) || (microsecond_timer == NULL))
    {
        return ATLAS_ERROR_NULL;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->i2c = i2c;
    sensor->microsecond_timer = microsecond_timer;
    sensor->reset_port = BNO085_NRST_GPIO_Port;
    sensor->reset_pin = BNO085_NRST_Pin;
    sensor->interrupt_port = BNO085_H_INTN_GPIO_Port;
    sensor->interrupt_pin = BNO085_H_INTN_Pin;
    /* STM32 HAL expects the seven-bit value shifted left and supplies R/W: 0x4A -> 0x94. */
    sensor->address_hal = (uint16_t)(ATLAS_BNO085_I2C_ADDRESS_7BIT << 1U);
    sensor->sample_callback = sample_callback;
    sensor->sample_context = sample_context;
    sensor->hal.open = atlas_bno085_hal_open;
    sensor->hal.close = atlas_bno085_hal_close;
    sensor->hal.read = atlas_bno085_hal_read;
    sensor->hal.write = atlas_bno085_hal_write;
    sensor->hal.getTimeUs = atlas_bno085_hal_time_us;

    clock_status = AtlasTime_StartCounter(microsecond_timer);
    if (clock_status != ATLAS_OK)
    {
        return clock_status;
    }
    sh2_status = sh2_open(&sensor->hal, atlas_bno085_async_callback, sensor);
    if (sh2_status != SH2_OK)
    {
        atlas_bno085_abort_session(sensor);
        return atlas_bno085_from_sh2(sh2_status);
    }
    sensor->session_open = true;
    if (sensor->transport_failed)
    {
        atlas_bno085_abort_session(sensor);
        return ATLAS_ERROR_IO;
    }
    sh2_status = sh2_setSensorCallback(atlas_bno085_sensor_callback, sensor);
    if (sh2_status != SH2_OK)
    {
        atlas_bno085_abort_session(sensor);
        return atlas_bno085_from_sh2(sh2_status);
    }
    sh2_status = sh2_getProdIds(&sensor->product_ids);
    if ((sh2_status != SH2_OK) || sensor->transport_failed)
    {
        const AtlasStatus status = sensor->transport_failed ?
                                   ATLAS_ERROR_IO : atlas_bno085_from_sh2(sh2_status);
        atlas_bno085_abort_session(sensor);
        return status;
    }
    if (sensor->product_ids.numEntries == 0U)
    {
        atlas_bno085_abort_session(sensor);
        return ATLAS_ERROR_IDENTITY;
    }

    sensor->initialized = true;
    return ATLAS_OK;
}

/**
 * @brief Enable or update one SH-2 report.
 * @param sensor Initialized BNO085 instance.
 * @param sensor_id SH-2 report identifier.
 * @param report_interval_us Nonzero interval between reports in microseconds.
 * @param batch_interval_us Batch latency; zero disables batching.
 * @return ATLAS_OK or a typed parameter/SH-2 failure.
 */
AtlasStatus AtlasBno085_EnableReport(AtlasBno085 *sensor,
                                     sh2_SensorId_t sensor_id,
                                     uint32_t report_interval_us,
                                     uint32_t batch_interval_us)
{
    sh2_SensorConfig_t config;
    int sh2_status;
    AtlasStatus status;

    if (sensor == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if ((!sensor->initialized) || (report_interval_us == 0U) ||
        (sensor_id > SH2_MAX_SENSOR_ID))
    {
        return sensor->initialized ? ATLAS_ERROR_ARGUMENT : ATLAS_ERROR_NOT_READY;
    }

    memset(&config, 0, sizeof(config));
    config.reportInterval_us = report_interval_us;
    config.batchInterval_us = batch_interval_us;
    sh2_status = sh2_setSensorConfig(sensor_id, &config);
    status = sensor->transport_failed ? ATLAS_ERROR_IO :
             atlas_bno085_from_sh2(sh2_status);
    if (status != ATLAS_OK)
    {
        atlas_bno085_abort_session(sensor);
    }
    return status;
}

/**
 * @brief Disable one SH-2 report.
 * @param sensor Initialized BNO085 instance.
 * @param sensor_id SH-2 report identifier to disable.
 * @return ATLAS_OK or a typed parameter/SH-2 failure.
 */
AtlasStatus AtlasBno085_DisableReport(AtlasBno085 *sensor,
                                      sh2_SensorId_t sensor_id)
{
    sh2_SensorConfig_t config;
    int sh2_status;
    AtlasStatus status;

    if (sensor == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (sensor_id > SH2_MAX_SENSOR_ID)
    {
        return ATLAS_ERROR_ARGUMENT;
    }
    memset(&config, 0, sizeof(config));
    sh2_status = sh2_setSensorConfig(sensor_id, &config);
    status = sensor->transport_failed ? ATLAS_ERROR_IO :
             atlas_bno085_from_sh2(sh2_status);
    if (status != ATLAS_OK)
    {
        atlas_bno085_abort_session(sensor);
    }
    return status;
}

/**
 * @brief Service a bounded number of pending BNO085 transfers.
 * @param sensor Initialized BNO085 instance.
 * @return ATLAS_OK or a typed readiness, transport, protocol, or reset-state failure.
 */
AtlasStatus AtlasBno085_Service(AtlasBno085 *sensor)
{
    uint32_t io_errors_before;
    uint32_t protocol_errors_before;
    uint32_t decode_errors_before;
    uint32_t async_resets_before;
    uint32_t count;

    if (sensor == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }

    io_errors_before = sensor->health.io_errors;
    protocol_errors_before = sensor->health.protocol_errors;
    decode_errors_before = sensor->health.decode_errors;
    async_resets_before = sensor->health.async_resets;

    for (count = 0U; count < ATLAS_BNO085_MAX_SERVICE_READS; ++count)
    {
        if (!atlas_bno085_read_phase_ready(sensor))
        {
            break;
        }
        sh2_service();
        if (sensor->transport_failed)
        {
            break;
        }
    }

    /* SH-2 reports failures through HAL/callback side effects, so compare counters. */
    if (sensor->health.io_errors != io_errors_before)
    {
        atlas_bno085_abort_session(sensor);
        return ATLAS_ERROR_IO;
    }
    if ((sensor->health.protocol_errors != protocol_errors_before) ||
        (sensor->health.decode_errors != decode_errors_before))
    {
        if (sensor->health.protocol_errors != protocol_errors_before)
        {
            atlas_bno085_abort_session(sensor);
        }
        return ATLAS_ERROR_PROTOCOL;
    }
    if (sensor->health.async_resets != async_resets_before)
    {
        /* A hub reset clears feature reports; the caller must deliberately reconfigure. */
        atlas_bno085_abort_session(sensor);
        return ATLAS_ERROR_STATE;
    }
    return ATLAS_OK;
}

/**
 * @brief Record the BNO085 active-low interrupt without performing bus I/O.
 * @param sensor Driver instance; NULL is ignored.
 */
void AtlasBno085_OnInterrupt(AtlasBno085 *sensor)
{
    if (sensor != NULL)
    {
        if (sensor->microsecond_timer != NULL)
        {
            sensor->interrupt_timestamp_us =
                __HAL_TIM_GET_COUNTER(sensor->microsecond_timer);
        }
        sensor->interrupt_rearmed = false;
        __DMB();
        sensor->interrupt_pending = true;
        ++sensor->health.interrupts;
    }
}

/**
 * @brief Close SH-2 and hold the BNO085 in reset.
 * @param sensor Driver instance; NULL is ignored.
 */
void AtlasBno085_Deinit(AtlasBno085 *sensor)
{
    if ((sensor != NULL) && sensor->session_open)
    {
        sh2_close();
        sensor->initialized = false;
    }
}
