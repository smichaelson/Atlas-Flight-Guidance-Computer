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

#define BNO085_I2C_TIMEOUT_MS       (25U)
#define BNO085_BOOT_TIMEOUT_MS      (4000U)
#define BNO085_RESET_ASSERT_MS      (10U)
#define BNO085_I2C_RETRY_COUNT      (3U)
#define BNO085_SHTP_HEADER_LENGTH   (4U)

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
 * @brief Perform a bounded I2C receive with retries for transient BNO085 NACKs.
 * @param sensor Driver instance.
 * @param data Destination buffer.
 * @param length Exact byte count.
 * @return HAL_OK on success or the last HAL status.
 */
static HAL_StatusTypeDef atlas_bno085_receive(AtlasBno085 *sensor,
                                              uint8_t *data,
                                              uint16_t length)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    uint32_t attempt;

    for (attempt = 0U; attempt < BNO085_I2C_RETRY_COUNT; ++attempt)
    {
        status = HAL_I2C_Master_Receive(sensor->i2c,
                                        sensor->address_hal,
                                        data,
                                        length,
                                        BNO085_I2C_TIMEOUT_MS);
        if (status == HAL_OK)
        {
            break;
        }
        /* The hub may briefly NACK while changing SHTP channel state. */
        AtlasTime_DelayMs(1U);
    }
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

    for (attempt = 0U; attempt < BNO085_I2C_RETRY_COUNT; ++attempt)
    {
        status = HAL_I2C_Master_Transmit(sensor->i2c,
                                         sensor->address_hal,
                                         data,
                                         length,
                                         BNO085_I2C_TIMEOUT_MS);
        if (status == HAL_OK)
        {
            break;
        }
        AtlasTime_DelayMs(1U);
    }
    return status;
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
    HAL_GPIO_WritePin(sensor->reset_port, sensor->reset_pin, GPIO_PIN_SET);

    while (HAL_GPIO_ReadPin(sensor->interrupt_port, sensor->interrupt_pin) != GPIO_PIN_RESET)
    {
        if ((HAL_GetTick() - started_ms) >= BNO085_BOOT_TIMEOUT_MS)
        {
            return SH2_ERR_TIMEOUT;
        }
        AtlasTime_DelayMs(1U);
    }
    sensor->interrupt_pending = true;
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
}

/**
 * @brief Read one complete SHTP I2C transfer after first reading its length header.
 * @param hal SH-2 HAL supplied by the CEVA stack.
 * @param buffer Destination SHTP transfer buffer.
 * @param capacity Destination capacity.
 * @param timestamp_us Destination interrupt/read timestamp.
 * @return Transfer byte count, zero when no transfer is pending, or a negative SH-2 error.
 */
static int atlas_bno085_hal_read(sh2_Hal_t *hal,
                                 uint8_t *buffer,
                                 unsigned capacity,
                                 uint32_t *timestamp_us)
{
    AtlasBno085 *sensor = atlas_bno085_from_hal(hal);
    uint8_t header[BNO085_SHTP_HEADER_LENGTH] = {0U};
    uint16_t transfer_length;

    if ((buffer == NULL) || (timestamp_us == NULL))
    {
        return SH2_ERR_BAD_PARAM;
    }
    if ((!sensor->interrupt_pending) &&
        (HAL_GPIO_ReadPin(sensor->interrupt_port, sensor->interrupt_pin) != GPIO_PIN_RESET))
    {
        return 0;
    }

    sensor->interrupt_pending = false;
    *timestamp_us = __HAL_TIM_GET_COUNTER(sensor->microsecond_timer);
    if (atlas_bno085_receive(sensor, header, sizeof(header)) != HAL_OK)
    {
        ++sensor->health.io_errors;
        return SH2_ERR_IO;
    }

    transfer_length = (uint16_t)(((uint16_t)header[1] << 8) | header[0]);
    transfer_length &= 0x7FFFU; /* Bit 15 marks SHTP continuation, not length. */
    if (transfer_length == 0U)
    {
        return 0;
    }
    if ((transfer_length < BNO085_SHTP_HEADER_LENGTH) ||
        ((unsigned)transfer_length > capacity))
    {
        ++sensor->health.protocol_errors;
        return SH2_ERR_BAD_PARAM;
    }

    /* Each BNO085 I2C read starts at a new SHTP header, so request the full frame. */
    if (atlas_bno085_receive(sensor, buffer, transfer_length) != HAL_OK)
    {
        ++sensor->health.io_errors;
        return SH2_ERR_IO;
    }
    if (((((uint16_t)buffer[1] << 8) | buffer[0]) & 0x7FFFU) != transfer_length)
    {
        ++sensor->health.protocol_errors;
        return SH2_ERR_IO;
    }

    ++sensor->health.transfers_read;
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
    if (atlas_bno085_transmit(sensor, buffer, (uint16_t)length) != HAL_OK)
    {
        ++sensor->health.io_errors;
        return SH2_ERR_IO;
    }
    ++sensor->health.transfers_written;
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
    sensor->address_hal = (uint16_t)(ATLAS_BNO085_I2C_ADDRESS_7BIT << 1);
    sensor->sample_callback = sample_callback;
    sensor->sample_context = sample_context;
    sensor->hal.open = atlas_bno085_hal_open;
    sensor->hal.close = atlas_bno085_hal_close;
    sensor->hal.read = atlas_bno085_hal_read;
    sensor->hal.write = atlas_bno085_hal_write;
    sensor->hal.getTimeUs = atlas_bno085_hal_time_us;

    if (HAL_TIM_Base_Start(microsecond_timer) != HAL_OK)
    {
        return ATLAS_ERROR_IO;
    }
    sh2_status = sh2_open(&sensor->hal, atlas_bno085_async_callback, sensor);
    if (sh2_status != SH2_OK)
    {
        return atlas_bno085_from_sh2(sh2_status);
    }
    sh2_status = sh2_setSensorCallback(atlas_bno085_sensor_callback, sensor);
    if (sh2_status != SH2_OK)
    {
        sh2_close();
        return atlas_bno085_from_sh2(sh2_status);
    }
    sh2_status = sh2_getProdIds(&sensor->product_ids);
    if (sh2_status != SH2_OK)
    {
        sh2_close();
        return atlas_bno085_from_sh2(sh2_status);
    }
    if (sensor->product_ids.numEntries == 0U)
    {
        sh2_close();
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
    return atlas_bno085_from_sh2(sh2_setSensorConfig(sensor_id, &config));
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
    return atlas_bno085_from_sh2(sh2_setSensorConfig(sensor_id, &config));
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
        if ((!sensor->interrupt_pending) &&
            (HAL_GPIO_ReadPin(sensor->interrupt_port, sensor->interrupt_pin) != GPIO_PIN_RESET))
        {
            break;
        }
        sh2_service();
    }

    /* SH-2 reports failures through HAL/callback side effects, so compare counters. */
    if (sensor->health.io_errors != io_errors_before)
    {
        return ATLAS_ERROR_IO;
    }
    if ((sensor->health.protocol_errors != protocol_errors_before) ||
        (sensor->health.decode_errors != decode_errors_before))
    {
        return ATLAS_ERROR_PROTOCOL;
    }
    if (sensor->health.async_resets != async_resets_before)
    {
        /* A hub reset clears feature reports; the caller must deliberately reconfigure. */
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
    if ((sensor != NULL) && sensor->initialized)
    {
        sh2_close();
        sensor->initialized = false;
    }
}
