/**
 * @file atlas_adxl375.c
 * @brief ADXL375 SPI register configuration, readback, and coherent XYZ acquisition.
 *
 * Major functions:
 * - AtlasAdxl375_Init(): checks 0xE5 identity and readbacks every critical setting.
 * - AtlasAdxl375_ReadSample(): burst-reads all axes to prevent torn samples.
 * - AtlasAdxl375_SetMeasurementEnabled(): controls the POWER_CTL Measure bit.
 */

#include "atlas_adxl375.h"

#include <string.h>

#define ADXL375_REG_DEVID       (0x00U)
#define ADXL375_REG_BW_RATE     (0x2CU)
#define ADXL375_REG_POWER_CTL   (0x2DU)
#define ADXL375_REG_INT_ENABLE  (0x2EU)
#define ADXL375_REG_INT_SOURCE  (0x30U)
#define ADXL375_REG_DATA_FORMAT (0x31U)
#define ADXL375_REG_DATAX0      (0x32U)

#define ADXL375_SPI_READ        (0x80U)
#define ADXL375_SPI_MULTIBYTE   (0x40U)
#define ADXL375_POWER_MEASURE   (0x08U)
#define ADXL375_INT_DATA_READY  (0x80U)

/* Fixed data-format bits D3, D1, and D0 must be one; SPI=0 selects 4-wire mode. */
#define ADXL375_FORMAT_4WIRE_RIGHT_JUSTIFIED (0x0BU)

/**
 * @brief Check whether an ODR code is supported by the public API.
 * @param odr Candidate ODR code.
 * @return true for a valid code; otherwise false.
 */
static bool atlas_adxl375_odr_valid(AtlasAdxl375Odr odr)
{
    return ((uint8_t)odr >= (uint8_t)ATLAS_ADXL375_ODR_25_HZ) &&
           ((uint8_t)odr <= (uint8_t)ATLAS_ADXL375_ODR_3200_HZ);
}

/**
 * @brief Read one or more adjacent ADXL375 registers.
 * @param sensor Driver instance.
 * @param reg First register address.
 * @param data Destination bytes.
 * @param length Number of bytes; range 1..6 for current uses.
 * @return ATLAS_OK or transport failure.
 */
static AtlasStatus atlas_adxl375_read(AtlasAdxl375 *sensor,
                                      uint8_t reg,
                                      uint8_t *data,
                                      size_t length)
{
    uint8_t tx[7] = {0U};
    uint8_t rx[7] = {0U};
    AtlasStatus status;

    if ((length == 0U) || (length > 6U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    tx[0] = (uint8_t)(ADXL375_SPI_READ | reg);
    if (length > 1U)
    {
        tx[0] |= ADXL375_SPI_MULTIBYTE;
    }

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
 * @brief Write one ADXL375 register.
 * @param sensor Driver instance.
 * @param reg Register address.
 * @param value New register value.
 * @return ATLAS_OK or transport failure.
 */
static AtlasStatus atlas_adxl375_write(AtlasAdxl375 *sensor,
                                       uint8_t reg,
                                       uint8_t value)
{
    const uint8_t tx[2] = {reg, value};
    uint8_t rx[2] = {0U};
    AtlasStatus status = AtlasSpiDevice_Transfer(&sensor->device, tx, rx, sizeof(tx));

    if (status != ATLAS_OK)
    {
        ++sensor->health.io_errors;
    }
    return status;
}

/**
 * @brief Write and read back a critical register using a comparison mask.
 * @param sensor Driver instance.
 * @param reg Register address.
 * @param value Value to write.
 * @param mask Bits that must match on readback.
 * @return ATLAS_OK, transport failure, or ATLAS_ERROR_PROTOCOL on mismatch.
 */
static AtlasStatus atlas_adxl375_write_checked(AtlasAdxl375 *sensor,
                                               uint8_t reg,
                                               uint8_t value,
                                               uint8_t mask)
{
    uint8_t actual = 0U;
    AtlasStatus status = atlas_adxl375_write(sensor, reg, value);

    if (status == ATLAS_OK)
    {
        status = atlas_adxl375_read(sensor, reg, &actual, 1U);
    }
    if ((status == ATLAS_OK) && ((actual & mask) != (value & mask)))
    {
        ++sensor->health.configuration_mismatches;
        return ATLAS_ERROR_PROTOCOL;
    }
    return status;
}

/**
 * @brief Verify and configure the Atlas ADXL375 in 4-wire SPI measurement mode.
 * @param sensor Destination driver instance.
 * @param spi Initialized SPI2 HAL handle configured for mode 3 at no more than 5 MHz.
 * @param cs_port ADXL375 chip-select GPIO port.
 * @param cs_pin ADXL375 active-low chip-select pin mask.
 * @param odr Requested normal-power output data rate.
 * @return ATLAS_OK or a typed identity, configuration, or transport failure.
 */
AtlasStatus AtlasAdxl375_Init(AtlasAdxl375 *sensor,
                              SPI_HandleTypeDef *spi,
                              GPIO_TypeDef *cs_port,
                              uint16_t cs_pin,
                              AtlasAdxl375Odr odr)
{
    uint8_t device_id = 0U;
    AtlasStatus status;

    if ((sensor == NULL) || (spi == NULL) || (cs_port == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!atlas_adxl375_odr_valid(odr))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    memset(sensor, 0, sizeof(*sensor));
    status = AtlasSpiDevice_Init(&sensor->device, spi, cs_port, cs_pin, 20U);
    if (status != ATLAS_OK)
    {
        return status;
    }

    status = atlas_adxl375_read(sensor, ADXL375_REG_DEVID, &device_id, 1U);
    if (status != ATLAS_OK)
    {
        return status;
    }
    if (device_id != ATLAS_ADXL375_DEVICE_ID)
    {
        ++sensor->health.identity_failures;
        return ATLAS_ERROR_IDENTITY;
    }

    /* Configure only in standby, as recommended by Analog Devices. */
    status = atlas_adxl375_write_checked(sensor,
                                         ADXL375_REG_POWER_CTL,
                                         0U,
                                         ADXL375_POWER_MEASURE);
    if (status == ATLAS_OK)
    {
        status = atlas_adxl375_write_checked(sensor,
                                             ADXL375_REG_DATA_FORMAT,
                                             ADXL375_FORMAT_4WIRE_RIGHT_JUSTIFIED,
                                             0xFFU);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_adxl375_write_checked(sensor,
                                             ADXL375_REG_BW_RATE,
                                             (uint8_t)odr,
                                             0x1FU);
    }
    if (status == ATLAS_OK)
    {
        /* Atlas does not route ADXL375 interrupt pins, so polling is explicit. */
        status = atlas_adxl375_write_checked(sensor,
                                             ADXL375_REG_INT_ENABLE,
                                             0U,
                                             0xFFU);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_adxl375_write_checked(sensor,
                                             ADXL375_REG_POWER_CTL,
                                             ADXL375_POWER_MEASURE,
                                             ADXL375_POWER_MEASURE);
    }
    if (status != ATLAS_OK)
    {
        return status;
    }

    sensor->odr = odr;
    sensor->initialized = true;
    return ATLAS_OK;
}

/**
 * @brief Enable measurement mode or return the sensor to standby.
 * @param sensor Initialized driver instance.
 * @param enabled true for measurement; false for standby.
 * @return ATLAS_OK or a typed transport/configuration failure.
 */
AtlasStatus AtlasAdxl375_SetMeasurementEnabled(AtlasAdxl375 *sensor, bool enabled)
{
    if (sensor == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    return atlas_adxl375_write_checked(sensor,
                                       ADXL375_REG_POWER_CTL,
                                       enabled ? ADXL375_POWER_MEASURE : 0U,
                                       ADXL375_POWER_MEASURE);
}

/**
 * @brief Check the DATA_READY source bit without changing configuration.
 * @param sensor Initialized driver instance.
 * @param ready Destination for DATA_READY state.
 * @return ATLAS_OK or a typed transport failure.
 */
AtlasStatus AtlasAdxl375_DataReady(AtlasAdxl375 *sensor, bool *ready)
{
    uint8_t source = 0U;
    AtlasStatus status;

    if ((sensor == NULL) || (ready == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }

    status = atlas_adxl375_read(sensor, ADXL375_REG_INT_SOURCE, &source, 1U);
    if (status == ATLAS_OK)
    {
        *ready = ((source & ADXL375_INT_DATA_READY) != 0U);
    }
    return status;
}

/**
 * @brief Read one coherent XYZ sample and convert it using the 49 mg/LSB nominal scale.
 * @param sensor Initialized driver instance.
 * @param sample Destination sample.
 * @return ATLAS_OK or a typed transport failure.
 */
AtlasStatus AtlasAdxl375_ReadSample(AtlasAdxl375 *sensor,
                                    AtlasAdxl375Sample *sample)
{
    uint8_t bytes[6];
    AtlasStatus status;

    if ((sensor == NULL) || (sample == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }

    /* One multibyte transaction prevents an ODR boundary from tearing XYZ data. */
    status = atlas_adxl375_read(sensor, ADXL375_REG_DATAX0, bytes, sizeof(bytes));
    if (status != ATLAS_OK)
    {
        return status;
    }

    sample->raw_x = (int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
    sample->raw_y = (int16_t)((uint16_t)bytes[2] | ((uint16_t)bytes[3] << 8));
    sample->raw_z = (int16_t)((uint16_t)bytes[4] | ((uint16_t)bytes[5] << 8));
    sample->x_g = (float)sample->raw_x * ATLAS_ADXL375_SCALE_G_PER_LSB;
    sample->y_g = (float)sample->raw_y * ATLAS_ADXL375_SCALE_G_PER_LSB;
    sample->z_g = (float)sample->raw_z * ATLAS_ADXL375_SCALE_G_PER_LSB;
    sample->timestamp_ms = HAL_GetTick();
    ++sensor->health.samples_read;
    return ATLAS_OK;
}
