/**
 * @file atlas_lsm6dsv16b.c
 * @brief Verified LSM6DSV16B mode-3 SPI setup and direct-register sampling.
 *
 * Major functions:
 * - AtlasLsm6dsv16b_Init(): performs software reset, identity check, and readback.
 * - AtlasLsm6dsv16b_ReadSample(): handles the device's unusual Z/Y/X accel map.
 * - AtlasLsm6dsv16b_OnInterrupt(): records an edge without bus work in the ISR.
 */

#include "atlas_lsm6dsv16b.h"

#include "atlas_time.h"

#include <string.h>

#define LSM6_REG_INT1_CTRL  (0x0DU)
#define LSM6_REG_WHO_AM_I   (0x0FU)
#define LSM6_REG_CTRL1      (0x10U)
#define LSM6_REG_CTRL2      (0x11U)
#define LSM6_REG_CTRL3      (0x12U)
#define LSM6_REG_CTRL6      (0x15U)
#define LSM6_REG_CTRL8      (0x17U)
#define LSM6_REG_STATUS     (0x1EU)
#define LSM6_REG_OUT_TEMP_L (0x20U)

#define LSM6_SPI_READ       (0x80U)
#define LSM6_CTRL3_BDU      (0x40U)
#define LSM6_CTRL3_IF_INC   (0x04U)
#define LSM6_CTRL3_SW_RESET (0x01U)
#define LSM6_STATUS_GDA     (0x02U)
#define LSM6_STATUS_XLDA    (0x01U)
#define LSM6_INT1_GYRO_DRDY (0x02U)
#define LSM6_INT1_XL_DRDY   (0x01U)

/**
 * @brief Read adjacent LSM6DSV16B registers through SPI.
 * @param sensor Driver instance.
 * @param reg First register address.
 * @param data Destination buffer.
 * @param length Number of bytes; range 1..14 for current uses.
 * @return ATLAS_OK or typed transport error.
 */
static AtlasStatus atlas_lsm6_read(AtlasLsm6dsv16b *sensor,
                                   uint8_t reg,
                                   uint8_t *data,
                                   size_t length)
{
    uint8_t tx[15] = {0U};
    uint8_t rx[15] = {0U};
    AtlasStatus status;

    if ((length == 0U) || (length > 14U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }
    tx[0] = (uint8_t)(LSM6_SPI_READ | reg);
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
 * @brief Write one LSM6DSV16B register.
 * @param sensor Driver instance.
 * @param reg Register address.
 * @param value Register value.
 * @return ATLAS_OK or typed transport error.
 */
static AtlasStatus atlas_lsm6_write(AtlasLsm6dsv16b *sensor,
                                    uint8_t reg,
                                    uint8_t value)
{
    const uint8_t tx[2] = {(uint8_t)(reg & 0x7FU), value};
    uint8_t rx[2] = {0U};
    AtlasStatus status = AtlasSpiDevice_Transfer(&sensor->device, tx, rx, sizeof(tx));

    if (status != ATLAS_OK)
    {
        ++sensor->health.io_errors;
    }
    return status;
}

/**
 * @brief Write and verify one configuration register under a comparison mask.
 * @param sensor Driver instance.
 * @param reg Register address.
 * @param value New value.
 * @param mask Bits that must read back identically.
 * @return ATLAS_OK, typed transport error, or protocol mismatch.
 */
static AtlasStatus atlas_lsm6_write_checked(AtlasLsm6dsv16b *sensor,
                                            uint8_t reg,
                                            uint8_t value,
                                            uint8_t mask)
{
    uint8_t actual = 0U;
    AtlasStatus status = atlas_lsm6_write(sensor, reg, value);

    if (status == ATLAS_OK)
    {
        status = atlas_lsm6_read(sensor, reg, &actual, 1U);
    }
    if ((status == ATLAS_OK) && ((actual & mask) != (value & mask)))
    {
        ++sensor->health.configuration_mismatches;
        return ATLAS_ERROR_PROTOCOL;
    }
    return status;
}

/**
 * @brief Decode a little-endian signed 16-bit register pair.
 * @param bytes Pointer to the least-significant byte.
 * @return Signed sample.
 */
static int16_t atlas_lsm6_i16(const uint8_t *bytes)
{
    return (int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

/**
 * @brief Return accelerometer sensitivity for the configured full scale.
 * @param range Full-scale code.
 * @return Nominal g per LSB.
 */
static float atlas_lsm6_accel_scale(AtlasLsm6dsv16bAccelRange range)
{
    switch (range)
    {
        case ATLAS_LSM6_ACCEL_2_G:  return 0.000061F;
        case ATLAS_LSM6_ACCEL_4_G:  return 0.000122F;
        case ATLAS_LSM6_ACCEL_8_G:  return 0.000244F;
        case ATLAS_LSM6_ACCEL_16_G: return 0.000488F;
        default:                    return 0.0F;
    }
}

/**
 * @brief Return gyroscope sensitivity for the configured full scale.
 * @param range Full-scale code.
 * @return Nominal degrees/second per LSB.
 */
static float atlas_lsm6_gyro_scale(AtlasLsm6dsv16bGyroRange range)
{
    switch (range)
    {
        case ATLAS_LSM6_GYRO_125_DPS:  return 0.004375F;
        case ATLAS_LSM6_GYRO_250_DPS:  return 0.00875F;
        case ATLAS_LSM6_GYRO_500_DPS:  return 0.0175F;
        case ATLAS_LSM6_GYRO_1000_DPS: return 0.035F;
        case ATLAS_LSM6_GYRO_2000_DPS: return 0.070F;
        case ATLAS_LSM6_GYRO_4000_DPS: return 0.140F;
        default:                       return 0.0F;
    }
}

/**
 * @brief Validate a requested direct-data-path configuration.
 * @param config Configuration to inspect.
 * @return true when all encoded values are supported.
 */
static bool atlas_lsm6_config_valid(const AtlasLsm6dsv16bConfig *config)
{
    if (config == NULL)
    {
        return false;
    }
    if ((config->accel_odr < ATLAS_LSM6_ODR_60_HZ) ||
        (config->accel_odr > ATLAS_LSM6_ODR_960_HZ) ||
        (config->gyro_odr < ATLAS_LSM6_ODR_60_HZ) ||
        (config->gyro_odr > ATLAS_LSM6_ODR_960_HZ))
    {
        return false;
    }
    return (atlas_lsm6_accel_scale(config->accel_range) > 0.0F) &&
           (atlas_lsm6_gyro_scale(config->gyro_range) > 0.0F);
}

/**
 * @brief Return the reviewed Atlas default: 240 Hz, +/-16 g, +/-2000 dps, INT1 DRDY.
 * @return Complete default configuration.
 */
AtlasLsm6dsv16bConfig AtlasLsm6dsv16b_DefaultConfig(void)
{
    AtlasLsm6dsv16bConfig config;

    config.accel_odr = ATLAS_LSM6_ODR_240_HZ;
    config.gyro_odr = ATLAS_LSM6_ODR_240_HZ;
    config.accel_range = ATLAS_LSM6_ACCEL_16_G;
    config.gyro_range = ATLAS_LSM6_GYRO_2000_DPS;
    config.route_data_ready_to_int1 = true;
    return config;
}

/**
 * @brief Reset, identify, configure, and read back the LSM6DSV16B.
 * @param sensor Destination driver instance.
 * @param spi Initialized SPI3 handle in mode 3 at no more than 10 MHz.
 * @param cs_port LSM6DSV16B chip-select GPIO port.
 * @param cs_pin Active-low chip-select pin mask.
 * @param config Requested direct data-path configuration.
 * @return ATLAS_OK or a typed identity, timeout, configuration, or I/O error.
 */
AtlasStatus AtlasLsm6dsv16b_Init(AtlasLsm6dsv16b *sensor,
                                 SPI_HandleTypeDef *spi,
                                 GPIO_TypeDef *cs_port,
                                 uint16_t cs_pin,
                                 const AtlasLsm6dsv16bConfig *config)
{
    uint8_t value = 0U;
    uint32_t started_ms;
    AtlasStatus status;

    if ((sensor == NULL) || (spi == NULL) || (cs_port == NULL) || (config == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!atlas_lsm6_config_valid(config))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    memset(sensor, 0, sizeof(*sensor));
    status = AtlasSpiDevice_Init(&sensor->device, spi, cs_port, cs_pin, 20U);
    if (status != ATLAS_OK)
    {
        return status;
    }

    status = atlas_lsm6_read(sensor, LSM6_REG_WHO_AM_I, &value, 1U);
    if (status != ATLAS_OK)
    {
        return status;
    }
    if (value != ATLAS_LSM6DSV16B_WHO_AM_I)
    {
        ++sensor->health.identity_failures;
        return ATLAS_ERROR_IDENTITY;
    }

    status = atlas_lsm6_write(sensor, LSM6_REG_CTRL3, LSM6_CTRL3_SW_RESET);
    started_ms = HAL_GetTick();
    while (status == ATLAS_OK)
    {
        status = atlas_lsm6_read(sensor, LSM6_REG_CTRL3, &value, 1U);
        if ((status == ATLAS_OK) && ((value & LSM6_CTRL3_SW_RESET) == 0U))
        {
            break;
        }
        if ((uint32_t)(HAL_GetTick() - started_ms) >= 50U)
        {
            ++sensor->health.reset_timeouts;
            return ATLAS_ERROR_TIMEOUT;
        }
        AtlasTime_DelayMs(1U);
    }
    if (status != ATLAS_OK)
    {
        return status;
    }

    /* BDU prevents torn words; IF_INC enables the verified 14-byte burst below. */
    status = atlas_lsm6_write_checked(sensor,
                                      LSM6_REG_CTRL3,
                                      LSM6_CTRL3_BDU | LSM6_CTRL3_IF_INC,
                                      LSM6_CTRL3_BDU | LSM6_CTRL3_IF_INC);
    if (status == ATLAS_OK)
    {
        status = atlas_lsm6_write_checked(sensor,
                                          LSM6_REG_CTRL8,
                                          (uint8_t)config->accel_range,
                                          0x03U);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_lsm6_write_checked(sensor,
                                          LSM6_REG_CTRL6,
                                          (uint8_t)config->gyro_range,
                                          0x0FU);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_lsm6_write_checked(sensor,
                                          LSM6_REG_CTRL1,
                                          (uint8_t)config->accel_odr,
                                          0x7FU);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_lsm6_write_checked(sensor,
                                          LSM6_REG_CTRL2,
                                          (uint8_t)config->gyro_odr,
                                          0x7FU);
    }
    if (status == ATLAS_OK)
    {
        const uint8_t int1 = config->route_data_ready_to_int1 ?
                             (LSM6_INT1_GYRO_DRDY | LSM6_INT1_XL_DRDY) : 0U;
        status = atlas_lsm6_write_checked(sensor, LSM6_REG_INT1_CTRL, int1, 0x03U);
    }
    if (status != ATLAS_OK)
    {
        return status;
    }

    sensor->config = *config;
    sensor->initialized = true;
    return ATLAS_OK;
}

/**
 * @brief Read STATUS_REG and report whether both accel and gyro have fresh data.
 * @param sensor Initialized driver instance.
 * @param ready Destination readiness flag.
 * @return ATLAS_OK or a typed transport error.
 */
AtlasStatus AtlasLsm6dsv16b_DataReady(AtlasLsm6dsv16b *sensor, bool *ready)
{
    uint8_t status_reg = 0U;
    AtlasStatus status;

    if ((sensor == NULL) || (ready == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    status = atlas_lsm6_read(sensor, LSM6_REG_STATUS, &status_reg, 1U);
    if (status == ATLAS_OK)
    {
        *ready = ((status_reg & (LSM6_STATUS_GDA | LSM6_STATUS_XLDA)) ==
                  (LSM6_STATUS_GDA | LSM6_STATUS_XLDA));
    }
    return status;
}

/**
 * @brief Burst-read and scale temperature, gyro XYZ, and accel XYZ.
 * @param sensor Initialized driver instance.
 * @param sample Destination sample.
 * @return ATLAS_OK, ATLAS_ERROR_NOT_READY, or a typed transport error.
 */
AtlasStatus AtlasLsm6dsv16b_ReadSample(AtlasLsm6dsv16b *sensor,
                                       AtlasLsm6dsv16bSample *sample)
{
    uint8_t bytes[14];
    bool ready = false;
    float accel_scale;
    float gyro_scale;
    AtlasStatus status;

    if ((sensor == NULL) || (sample == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }

    status = AtlasLsm6dsv16b_DataReady(sensor, &ready);
    if (status != ATLAS_OK)
    {
        return status;
    }
    if (!ready)
    {
        return ATLAS_ERROR_NOT_READY;
    }

    status = atlas_lsm6_read(sensor, LSM6_REG_OUT_TEMP_L, bytes, sizeof(bytes));
    if (status != ATLAS_OK)
    {
        return status;
    }

    sample->raw_temperature = atlas_lsm6_i16(&bytes[0]);
    sample->raw_gyro_x = atlas_lsm6_i16(&bytes[2]);
    sample->raw_gyro_y = atlas_lsm6_i16(&bytes[4]);
    sample->raw_gyro_z = atlas_lsm6_i16(&bytes[6]);
    /* This part maps the direct accel registers as Z, Y, X at 0x28..0x2D. */
    sample->raw_accel_z = atlas_lsm6_i16(&bytes[8]);
    sample->raw_accel_y = atlas_lsm6_i16(&bytes[10]);
    sample->raw_accel_x = atlas_lsm6_i16(&bytes[12]);

    accel_scale = atlas_lsm6_accel_scale(sensor->config.accel_range);
    gyro_scale = atlas_lsm6_gyro_scale(sensor->config.gyro_range);
    sample->temperature_c = 25.0F + ((float)sample->raw_temperature / 256.0F);
    sample->gyro_x_dps = (float)sample->raw_gyro_x * gyro_scale;
    sample->gyro_y_dps = (float)sample->raw_gyro_y * gyro_scale;
    sample->gyro_z_dps = (float)sample->raw_gyro_z * gyro_scale;
    sample->accel_x_g = (float)sample->raw_accel_x * accel_scale;
    sample->accel_y_g = (float)sample->raw_accel_y * accel_scale;
    sample->accel_z_g = (float)sample->raw_accel_z * accel_scale;
    sample->timestamp_ms = HAL_GetTick();
    ++sensor->health.samples_read;
    return ATLAS_OK;
}

/**
 * @brief Record an INT1 edge; safe to call from HAL_GPIO_EXTI_Callback.
 * @param sensor Initialized driver instance.
 */
void AtlasLsm6dsv16b_OnInterrupt(AtlasLsm6dsv16b *sensor)
{
    if (sensor != NULL)
    {
        ++sensor->interrupt_count;
        ++sensor->health.interrupt_count;
        __DMB();
    }
}

/**
 * @brief Consume coalesced INT1 activity in foreground code.
 * @param sensor Initialized driver instance.
 * @return true when one or more new edges occurred since the prior call.
 */
bool AtlasLsm6dsv16b_ConsumeInterrupt(AtlasLsm6dsv16b *sensor)
{
    uint32_t count;

    if (sensor == NULL)
    {
        return false;
    }
    count = sensor->interrupt_count;
    if (count == sensor->interrupt_count_consumed)
    {
        return false;
    }
    sensor->interrupt_count_consumed = count;
    return true;
}
