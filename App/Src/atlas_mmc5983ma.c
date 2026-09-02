/**
 * @file atlas_mmc5983ma.c
 * @brief MMC5983MA mode-3 SPI setup, bounded conversion polling, and scaling.
 *
 * Major functions:
 * - AtlasMmc5983ma_Init(): validates product ID 0x30 and readbacks bandwidth.
 * - AtlasMmc5983ma_ReadField(): collects an automatic SET/RESET sample.
 * - AtlasMmc5983ma_ReadFieldSetReset(): removes bridge offset by differencing.
 */

#include "atlas_mmc5983ma.h"

#include "atlas_time.h"

#include <stdbool.h>
#include <string.h>

#define MMC5983_REG_XOUT0       (0x00U)
#define MMC5983_REG_TOUT        (0x07U)
#define MMC5983_REG_STATUS      (0x08U)
#define MMC5983_REG_CONTROL0    (0x09U)
#define MMC5983_REG_CONTROL1    (0x0AU)
#define MMC5983_REG_PRODUCT_ID  (0x2FU)

#define MMC5983_SPI_READ        (0x80U)
#define MMC5983_STATUS_MEAS_DONE (0x01U)
#define MMC5983_STATUS_TEMP_DONE (0x02U)
#define MMC5983_CONTROL0_TM_M   (0x01U)
#define MMC5983_CONTROL0_TM_T   (0x02U)
#define MMC5983_CONTROL0_SET    (0x08U)
#define MMC5983_CONTROL0_RESET  (0x10U)
#define MMC5983_CONTROL0_AUTO_SR (0x20U)
#define MMC5983_CONTROL1_SW_RST (0x80U)

/**
 * @brief Read adjacent MMC5983MA registers.
 * @param sensor Driver instance.
 * @param reg First register.
 * @param data Destination.
 * @param length Byte count; range 1..7 for current uses.
 * @return ATLAS_OK or typed transport failure.
 */
static AtlasStatus atlas_mmc5983_read(AtlasMmc5983ma *sensor,
                                      uint8_t reg,
                                      uint8_t *data,
                                      size_t length)
{
    uint8_t tx[8] = {0U};
    uint8_t rx[8] = {0U};
    AtlasStatus status;

    if ((length == 0U) || (length > 7U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }
    tx[0] = (uint8_t)(MMC5983_SPI_READ | reg);
    status = AtlasSpiDevice_Transfer(&sensor->device, tx, rx, length + 1U);
    if (status != ATLAS_OK)
    {
        ++sensor->health.io_errors;
        return status;
    }
    memcpy(data, &rx[1], length);
    return ATLAS_OK;
}

/**
 * @brief Write one MMC5983MA register.
 * @param sensor Driver instance.
 * @param reg Register address.
 * @param value Register value.
 * @return ATLAS_OK or typed transport failure.
 */
static AtlasStatus atlas_mmc5983_write(AtlasMmc5983ma *sensor,
                                       uint8_t reg,
                                       uint8_t value)
{
    const uint8_t tx[2] = {(uint8_t)(reg & 0x3FU), value};
    uint8_t rx[2] = {0U};
    AtlasStatus status = AtlasSpiDevice_Transfer(&sensor->device, tx, rx, sizeof(tx));

    if (status != ATLAS_OK)
    {
        ++sensor->health.io_errors;
    }
    return status;
}

/**
 * @brief Poll one STATUS register bit with a wrap-safe deadline.
 * @param sensor Driver instance.
 * @param mask Completion bit.
 * @param timeout_ms Nonzero deadline duration.
 * @return ATLAS_OK, ATLAS_ERROR_TIMEOUT, or transport failure.
 */
static AtlasStatus atlas_mmc5983_wait_status(AtlasMmc5983ma *sensor,
                                             uint8_t mask,
                                             uint32_t timeout_ms)
{
    uint8_t status_reg = 0U;
    uint32_t started_ms = HAL_GetTick();
    AtlasStatus status;

    do
    {
        status = atlas_mmc5983_read(sensor, MMC5983_REG_STATUS, &status_reg, 1U);
        if (status != ATLAS_OK)
        {
            return status;
        }
        if ((status_reg & mask) != 0U)
        {
            return ATLAS_OK;
        }
        AtlasTime_DelayMs(1U);
    } while ((uint32_t)(HAL_GetTick() - started_ms) < timeout_ms);

    ++sensor->health.measurement_timeouts;
    return ATLAS_ERROR_TIMEOUT;
}

/**
 * @brief Read and unpack one completed magnetic measurement.
 * @param sensor Driver instance.
 * @param raw Destination raw values.
 * @return ATLAS_OK or typed transport/protocol failure.
 */
static AtlasStatus atlas_mmc5983_read_raw(AtlasMmc5983ma *sensor,
                                          AtlasMmc5983maRaw *raw)
{
    uint8_t bytes[7];
    AtlasStatus status = atlas_mmc5983_read(sensor,
                                            MMC5983_REG_XOUT0,
                                            bytes,
                                            sizeof(bytes));
    if (status != ATLAS_OK)
    {
        return status;
    }
    return AtlasMmc5983ma_UnpackRaw(bytes, raw);
}

/**
 * @brief Trigger a manual SET or RESET pulse and allow the bridge to settle.
 * @param sensor Driver instance.
 * @param control_bit MMC5983_CONTROL0_SET or MMC5983_CONTROL0_RESET.
 * @return ATLAS_OK or typed transport failure.
 */
static AtlasStatus atlas_mmc5983_pulse_bridge(AtlasMmc5983ma *sensor,
                                              uint8_t control_bit)
{
    AtlasStatus status = atlas_mmc5983_write(sensor, MMC5983_REG_CONTROL0, control_bit);
    if (status == ATLAS_OK)
    {
        /* One millisecond exceeds the specified SET/RESET pulse recovery interval. */
        AtlasTime_DelayMs(1U);
    }
    return status;
}

/**
 * @brief Trigger and collect one raw magnetic conversion.
 * @param sensor Driver instance.
 * @param command Control 0 command containing TM_M and optional Auto_SR.
 * @param raw Destination raw values.
 * @param timeout_ms Bounded completion timeout.
 * @return ATLAS_OK or typed timeout/transport failure.
 */
static AtlasStatus atlas_mmc5983_measure_raw(AtlasMmc5983ma *sensor,
                                             uint8_t command,
                                             AtlasMmc5983maRaw *raw,
                                             uint32_t timeout_ms)
{
    AtlasStatus status = atlas_mmc5983_write(sensor, MMC5983_REG_CONTROL0, command);
    if (status == ATLAS_OK)
    {
        status = atlas_mmc5983_wait_status(sensor, MMC5983_STATUS_MEAS_DONE, timeout_ms);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_mmc5983_read_raw(sensor, raw);
    }
    return status;
}

/**
 * @brief Software-reset, identify, configure, and verify the MMC5983MA.
 * @param sensor Destination driver instance.
 * @param spi Initialized SPI2 handle in mode 3 at no more than 10 MHz.
 * @param cs_port MMC5983MA chip-select GPIO port.
 * @param cs_pin Active-low chip-select pin mask.
 * @param bandwidth Requested conversion bandwidth/latency.
 * @return ATLAS_OK or a typed identity, configuration, or transport failure.
 */
AtlasStatus AtlasMmc5983ma_Init(AtlasMmc5983ma *sensor,
                                SPI_HandleTypeDef *spi,
                                GPIO_TypeDef *cs_port,
                                uint16_t cs_pin,
                                AtlasMmc5983maBandwidth bandwidth)
{
    uint8_t value = 0U;
    AtlasStatus status;

    if ((sensor == NULL) || (spi == NULL) || (cs_port == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if ((uint8_t)bandwidth > (uint8_t)ATLAS_MMC5983_BW_800_HZ_0_5_MS)
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    memset(sensor, 0, sizeof(*sensor));
    status = AtlasSpiDevice_Init(&sensor->device, spi, cs_port, cs_pin, 20U);
    if (status != ATLAS_OK)
    {
        return status;
    }

    status = atlas_mmc5983_write(sensor, MMC5983_REG_CONTROL1, MMC5983_CONTROL1_SW_RST);
    if (status != ATLAS_OK)
    {
        return status;
    }
    AtlasTime_DelayMs(10U);

    status = atlas_mmc5983_read(sensor, MMC5983_REG_PRODUCT_ID, &value, 1U);
    if (status != ATLAS_OK)
    {
        return status;
    }
    if (value != ATLAS_MMC5983MA_PRODUCT_ID)
    {
        ++sensor->health.identity_failures;
        return ATLAS_ERROR_IDENTITY;
    }

    status = atlas_mmc5983_write(sensor, MMC5983_REG_CONTROL1, (uint8_t)bandwidth);
    if (status == ATLAS_OK)
    {
        status = atlas_mmc5983_read(sensor, MMC5983_REG_CONTROL1, &value, 1U);
    }
    if ((status == ATLAS_OK) && ((value & 0x03U) != (uint8_t)bandwidth))
    {
        ++sensor->health.configuration_mismatches;
        return ATLAS_ERROR_PROTOCOL;
    }
    if (status != ATLAS_OK)
    {
        return status;
    }

    sensor->bandwidth = bandwidth;
    sensor->initialized = true;
    return ATLAS_OK;
}

/**
 * @brief Decode the sensor's packed seven-byte 18-bit XYZ representation.
 * @param bytes Seven bytes from registers 0x00 through 0x06.
 * @param raw Destination raw values.
 * @return ATLAS_OK or a parameter error.
 */
AtlasStatus AtlasMmc5983ma_UnpackRaw(const uint8_t bytes[7],
                                     AtlasMmc5983maRaw *raw)
{
    if ((bytes == NULL) || (raw == NULL))
    {
        return ATLAS_ERROR_NULL;
    }

    raw->x = ((uint32_t)bytes[0] << 10) |
             ((uint32_t)bytes[1] << 2) |
             (((uint32_t)bytes[6] >> 6) & 0x03U);
    raw->y = ((uint32_t)bytes[2] << 10) |
             ((uint32_t)bytes[3] << 2) |
             (((uint32_t)bytes[6] >> 4) & 0x03U);
    raw->z = ((uint32_t)bytes[4] << 10) |
             ((uint32_t)bytes[5] << 2) |
             (((uint32_t)bytes[6] >> 2) & 0x03U);
    return ATLAS_OK;
}

/**
 * @brief Trigger one magnetic conversion with automatic bridge SET/RESET.
 * @param sensor Initialized driver instance.
 * @param field Destination field sample.
 * @param timeout_ms Nonzero bounded completion timeout.
 * @return ATLAS_OK or a typed timeout/transport failure.
 */
AtlasStatus AtlasMmc5983ma_ReadField(AtlasMmc5983ma *sensor,
                                     AtlasMmc5983maField *field,
                                     uint32_t timeout_ms)
{
    AtlasStatus status;

    if ((sensor == NULL) || (field == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (timeout_ms == 0U)
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    status = atlas_mmc5983_measure_raw(sensor,
                                       MMC5983_CONTROL0_AUTO_SR | MMC5983_CONTROL0_TM_M,
                                       &field->raw,
                                       timeout_ms);
    if (status != ATLAS_OK)
    {
        return status;
    }

    field->x_gauss = ((float)((int32_t)field->raw.x - ATLAS_MMC5983MA_ZERO_FIELD_COUNTS)) /
                     ATLAS_MMC5983MA_COUNTS_PER_GAUSS;
    field->y_gauss = ((float)((int32_t)field->raw.y - ATLAS_MMC5983MA_ZERO_FIELD_COUNTS)) /
                     ATLAS_MMC5983MA_COUNTS_PER_GAUSS;
    field->z_gauss = ((float)((int32_t)field->raw.z - ATLAS_MMC5983MA_ZERO_FIELD_COUNTS)) /
                     ATLAS_MMC5983MA_COUNTS_PER_GAUSS;
    field->timestamp_ms = HAL_GetTick();
    ++sensor->health.measurements;
    return ATLAS_OK;
}

/**
 * @brief Perform explicit SET and RESET conversions and cancel bridge offset.
 * @param sensor Initialized driver instance.
 * @param field Destination compensated field; raw contains the SET reading.
 * @param timeout_ms Nonzero timeout applied independently to each conversion.
 * @return ATLAS_OK or a typed timeout/transport failure.
 */
AtlasStatus AtlasMmc5983ma_ReadFieldSetReset(AtlasMmc5983ma *sensor,
                                             AtlasMmc5983maField *field,
                                             uint32_t timeout_ms)
{
    AtlasMmc5983maRaw reset_raw;
    AtlasStatus status;

    if ((sensor == NULL) || (field == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (timeout_ms == 0U)
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    status = atlas_mmc5983_pulse_bridge(sensor, MMC5983_CONTROL0_SET);
    if (status == ATLAS_OK)
    {
        status = atlas_mmc5983_measure_raw(sensor,
                                           MMC5983_CONTROL0_TM_M,
                                           &field->raw,
                                           timeout_ms);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_mmc5983_pulse_bridge(sensor, MMC5983_CONTROL0_RESET);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_mmc5983_measure_raw(sensor,
                                           MMC5983_CONTROL0_TM_M,
                                           &reset_raw,
                                           timeout_ms);
    }
    if (status != ATLAS_OK)
    {
        return status;
    }

    /* SET=(+H+offset), RESET=(-H+offset); half their difference is H. */
    field->x_gauss = ((float)((int32_t)field->raw.x - (int32_t)reset_raw.x)) /
                     (2.0F * ATLAS_MMC5983MA_COUNTS_PER_GAUSS);
    field->y_gauss = ((float)((int32_t)field->raw.y - (int32_t)reset_raw.y)) /
                     (2.0F * ATLAS_MMC5983MA_COUNTS_PER_GAUSS);
    field->z_gauss = ((float)((int32_t)field->raw.z - (int32_t)reset_raw.z)) /
                     (2.0F * ATLAS_MMC5983MA_COUNTS_PER_GAUSS);
    field->timestamp_ms = HAL_GetTick();
    ++sensor->health.measurements;
    return ATLAS_OK;
}

/**
 * @brief Trigger and read the internal temperature measurement.
 * @param sensor Initialized driver instance.
 * @param temperature_c Destination temperature in degrees Celsius.
 * @param timeout_ms Nonzero bounded completion timeout.
 * @return ATLAS_OK or a typed timeout/transport failure.
 */
AtlasStatus AtlasMmc5983ma_ReadTemperature(AtlasMmc5983ma *sensor,
                                           float *temperature_c,
                                           uint32_t timeout_ms)
{
    uint8_t raw = 0U;
    AtlasStatus status;

    if ((sensor == NULL) || (temperature_c == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (timeout_ms == 0U)
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    status = atlas_mmc5983_write(sensor, MMC5983_REG_CONTROL0, MMC5983_CONTROL0_TM_T);
    if (status == ATLAS_OK)
    {
        status = atlas_mmc5983_wait_status(sensor, MMC5983_STATUS_TEMP_DONE, timeout_ms);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_mmc5983_read(sensor, MMC5983_REG_TOUT, &raw, 1U);
    }
    if (status == ATLAS_OK)
    {
        *temperature_c = -75.0F + (0.8F * (float)raw);
    }
    return status;
}
