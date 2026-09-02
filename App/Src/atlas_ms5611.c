/**
 * @file atlas_ms5611.c
 * @brief MS5611 I2C command sequencing, PROM CRC-4, and 64-bit compensation.
 *
 * Major functions:
 * - AtlasMs5611_Init(): validates the only available identity evidence, PROM CRC-4.
 * - AtlasMs5611_Read(): runs D2 then D1 with conservative conversion delays.
 * - AtlasMs5611_Compensate(): prevents overflow with signed 64-bit intermediates.
 */

#include "atlas_ms5611.h"

#include "atlas_time.h"

#include <limits.h>
#include <string.h>

#define MS5611_COMMAND_RESET       (0x1EU)
#define MS5611_COMMAND_ADC_READ    (0x00U)
#define MS5611_COMMAND_PROM_BASE   (0xA0U)
#define MS5611_COMMAND_CONVERT_D1  (0x40U)
#define MS5611_COMMAND_CONVERT_D2  (0x50U)
#define MS5611_I2C_TIMEOUT_MS      (20U)

/**
 * @brief Translate a HAL I2C result into an Atlas status and health counter.
 * @param sensor Driver instance.
 * @param hal_status HAL result.
 * @return Corresponding Atlas status.
 */
static AtlasStatus atlas_ms5611_from_hal(AtlasMs5611 *sensor,
                                         HAL_StatusTypeDef hal_status)
{
    if (hal_status == HAL_OK)
    {
        return ATLAS_OK;
    }
    ++sensor->health.io_errors;
    if (hal_status == HAL_BUSY)
    {
        return ATLAS_ERROR_BUSY;
    }
    if (hal_status == HAL_TIMEOUT)
    {
        return ATLAS_ERROR_TIMEOUT;
    }
    return ATLAS_ERROR_IO;
}

/**
 * @brief Send one command byte.
 * @param sensor Driver instance.
 * @param command Command byte.
 * @return ATLAS_OK or typed transport failure.
 */
static AtlasStatus atlas_ms5611_command(AtlasMs5611 *sensor, uint8_t command)
{
    return atlas_ms5611_from_hal(sensor,
                                 HAL_I2C_Master_Transmit(sensor->i2c,
                                                         sensor->address_hal,
                                                         &command,
                                                         1U,
                                                         MS5611_I2C_TIMEOUT_MS));
}

/**
 * @brief Read one 16-bit PROM word in device byte order.
 * @param sensor Driver instance.
 * @param index PROM index 0..7.
 * @param value Destination word.
 * @return ATLAS_OK or typed transport failure.
 */
static AtlasStatus atlas_ms5611_read_prom(AtlasMs5611 *sensor,
                                          uint8_t index,
                                          uint16_t *value)
{
    uint8_t bytes[2] = {0U};
    AtlasStatus status = atlas_ms5611_command(sensor,
                                              (uint8_t)(MS5611_COMMAND_PROM_BASE +
                                                        (2U * index)));
    if (status == ATLAS_OK)
    {
        status = atlas_ms5611_from_hal(sensor,
                                       HAL_I2C_Master_Receive(sensor->i2c,
                                                              sensor->address_hal,
                                                              bytes,
                                                              sizeof(bytes),
                                                              MS5611_I2C_TIMEOUT_MS));
    }
    if (status == ATLAS_OK)
    {
        *value = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    }
    return status;
}

/**
 * @brief Map an OSR to its command offset and conservative wait time.
 * @param osr Requested oversampling.
 * @param command_offset Destination command offset.
 * @param wait_ms Destination conservative delay in milliseconds.
 * @return true for a supported OSR.
 */
static bool atlas_ms5611_osr_parameters(AtlasMs5611Osr osr,
                                        uint8_t *command_offset,
                                        uint32_t *wait_ms)
{
    static const uint8_t offsets[5] = {0x00U, 0x02U, 0x04U, 0x06U, 0x08U};
    static const uint8_t waits_ms[5] = {1U, 2U, 3U, 5U, 10U};

    if ((command_offset == NULL) || (wait_ms == NULL) ||
        ((unsigned)osr >= (sizeof(offsets) / sizeof(offsets[0]))))
    {
        return false;
    }
    *command_offset = offsets[(unsigned)osr];
    *wait_ms = waits_ms[(unsigned)osr];
    return true;
}

/**
 * @brief Trigger one ADC conversion and read its 24-bit result.
 * @param sensor Driver instance.
 * @param base_command D1 or D2 conversion base command.
 * @param osr Oversampling selection.
 * @param value Destination 24-bit conversion.
 * @return ATLAS_OK or typed transport/parameter failure.
 */
static AtlasStatus atlas_ms5611_convert(AtlasMs5611 *sensor,
                                        uint8_t base_command,
                                        AtlasMs5611Osr osr,
                                        uint32_t *value)
{
    uint8_t command_offset = 0U;
    uint32_t wait_ms = 0U;
    uint8_t bytes[3] = {0U};
    AtlasStatus status;

    if (!atlas_ms5611_osr_parameters(osr, &command_offset, &wait_ms))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    status = atlas_ms5611_command(sensor, (uint8_t)(base_command + command_offset));
    if (status != ATLAS_OK)
    {
        return status;
    }
    /* Rounded-up waits exceed the maximum conversion times in the TE data sheet. */
    AtlasTime_DelayMs(wait_ms);

    status = atlas_ms5611_command(sensor, MS5611_COMMAND_ADC_READ);
    if (status == ATLAS_OK)
    {
        status = atlas_ms5611_from_hal(sensor,
                                       HAL_I2C_Master_Receive(sensor->i2c,
                                                              sensor->address_hal,
                                                              bytes,
                                                              sizeof(bytes),
                                                              MS5611_I2C_TIMEOUT_MS));
    }
    if (status == ATLAS_OK)
    {
        *value = ((uint32_t)bytes[0] << 16) |
                 ((uint32_t)bytes[1] << 8) |
                 bytes[2];
    }
    return status;
}

/**
 * @brief Reset the Atlas MS5611, read all PROM words, and verify CRC-4.
 * @param sensor Destination driver instance.
 * @param i2c Initialized I2C1 handle at no more than 400 kHz.
 * @return ATLAS_OK or a typed transport, PROM, or CRC failure.
 */
AtlasStatus AtlasMs5611_Init(AtlasMs5611 *sensor, I2C_HandleTypeDef *i2c)
{
    uint8_t index;
    bool all_zero = true;
    bool all_ones = true;
    AtlasStatus status;

    if ((sensor == NULL) || (i2c == NULL))
    {
        return ATLAS_ERROR_NULL;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->i2c = i2c;
    sensor->address_hal = (uint16_t)(ATLAS_MS5611_I2C_ADDRESS_7BIT << 1);

    status = atlas_ms5611_command(sensor, MS5611_COMMAND_RESET);
    if (status != ATLAS_OK)
    {
        return status;
    }
    AtlasTime_DelayMs(3U);

    for (index = 0U; index < 8U; ++index)
    {
        status = atlas_ms5611_read_prom(sensor, index, &sensor->prom[index]);
        if (status != ATLAS_OK)
        {
            return status;
        }
        all_zero = all_zero && (sensor->prom[index] == 0U);
        all_ones = all_ones && (sensor->prom[index] == UINT16_MAX);
    }
    if (all_zero || all_ones)
    {
        ++sensor->health.invalid_prom_failures;
        return ATLAS_ERROR_IDENTITY;
    }

    /* MS5611 has no ID register; a valid factory PROM CRC is the identity check. */
    if (AtlasMs5611_Crc4(sensor->prom) != (uint8_t)(sensor->prom[7] & 0x0FU))
    {
        ++sensor->health.prom_crc_failures;
        return ATLAS_ERROR_CRC;
    }

    sensor->initialized = true;
    return ATLAS_OK;
}

/**
 * @brief Compute the data-sheet CRC-4 nibble for eight PROM words.
 * @param prom Eight PROM words, including the stored CRC in word 7.
 * @return Calculated CRC nibble in bits 3:0; returns 0 for NULL.
 */
uint8_t AtlasMs5611_Crc4(const uint16_t prom[8])
{
    uint16_t copy[8];
    uint16_t remainder = 0U;
    uint8_t byte_index;
    uint8_t bit_index;

    if (prom == NULL)
    {
        return 0U;
    }
    memcpy(copy, prom, sizeof(copy));
    copy[7] &= 0xFF00U;

    for (byte_index = 0U; byte_index < 16U; ++byte_index)
    {
        if ((byte_index & 1U) == 0U)
        {
            remainder ^= (uint16_t)(copy[byte_index >> 1] >> 8);
        }
        else
        {
            remainder ^= (uint16_t)(copy[byte_index >> 1] & 0x00FFU);
        }
        for (bit_index = 0U; bit_index < 8U; ++bit_index)
        {
            remainder = ((remainder & 0x8000U) != 0U) ?
                        (uint16_t)((remainder << 1) ^ 0x3000U) :
                        (uint16_t)(remainder << 1);
        }
    }
    return (uint8_t)((remainder >> 12) & 0x0FU);
}

/**
 * @brief Apply first- and second-order data-sheet compensation.
 * @param prom Eight PROM words with coefficients C1..C6 at indices 1..6.
 * @param raw_pressure_d1 Raw 24-bit pressure conversion.
 * @param raw_temperature_d2 Raw 24-bit temperature conversion.
 * @param pressure_pa Destination pressure in pascals.
 * @param temperature_centi_c Destination temperature in hundredths of a degree Celsius.
 * @return ATLAS_OK or a parameter error.
 */
AtlasStatus AtlasMs5611_Compensate(const uint16_t prom[8],
                                   uint32_t raw_pressure_d1,
                                   uint32_t raw_temperature_d2,
                                   int32_t *pressure_pa,
                                   int32_t *temperature_centi_c)
{
    int64_t delta_t;
    int64_t temperature;
    int64_t offset;
    int64_t sensitivity;
    int64_t temperature_2 = 0;
    int64_t offset_2 = 0;
    int64_t sensitivity_2 = 0;
    int64_t low_temp_delta;
    int64_t pressure;

    if ((prom == NULL) || (pressure_pa == NULL) || (temperature_centi_c == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if ((raw_pressure_d1 > 0xFFFFFFUL) || (raw_temperature_d2 > 0xFFFFFFUL))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    delta_t = (int64_t)raw_temperature_d2 - ((int64_t)prom[5] << 8);
    temperature = 2000 + ((delta_t * (int64_t)prom[6]) >> 23);
    offset = ((int64_t)prom[2] << 16) + (((int64_t)prom[4] * delta_t) >> 7);
    sensitivity = ((int64_t)prom[1] << 15) + (((int64_t)prom[3] * delta_t) >> 8);

    if (temperature < 2000)
    {
        const int64_t delta_20 = temperature - 2000;
        temperature_2 = (delta_t * delta_t) >> 31;
        offset_2 = (5 * delta_20 * delta_20) >> 1;
        sensitivity_2 = (5 * delta_20 * delta_20) >> 2;

        if (temperature < -1500)
        {
            low_temp_delta = temperature + 1500;
            offset_2 += 7 * low_temp_delta * low_temp_delta;
            sensitivity_2 += (11 * low_temp_delta * low_temp_delta) >> 1;
        }
    }

    temperature -= temperature_2;
    offset -= offset_2;
    sensitivity -= sensitivity_2;
    /* The data-sheet result is in 0.01 mbar, numerically identical to pascals. */
    pressure = ((((int64_t)raw_pressure_d1 * sensitivity) >> 21) - offset) >> 15;

    if ((pressure < INT32_MIN) || (pressure > INT32_MAX) ||
        (temperature < INT32_MIN) || (temperature > INT32_MAX))
    {
        return ATLAS_ERROR_OVERFLOW;
    }
    *pressure_pa = (int32_t)pressure;
    *temperature_centi_c = (int32_t)temperature;
    return ATLAS_OK;
}

/**
 * @brief Perform temperature and pressure conversions and return compensated data.
 * @param sensor Initialized driver instance.
 * @param osr Oversampling selection applied to both conversions.
 * @param sample Destination sample.
 * @return ATLAS_OK or a typed transport/parameter error.
 */
AtlasStatus AtlasMs5611_Read(AtlasMs5611 *sensor,
                             AtlasMs5611Osr osr,
                             AtlasMs5611Sample *sample)
{
    AtlasStatus status;

    if ((sensor == NULL) || (sample == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!sensor->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }

    status = atlas_ms5611_convert(sensor,
                                  MS5611_COMMAND_CONVERT_D2,
                                  osr,
                                  &sample->raw_temperature_d2);
    if (status == ATLAS_OK)
    {
        status = atlas_ms5611_convert(sensor,
                                      MS5611_COMMAND_CONVERT_D1,
                                      osr,
                                      &sample->raw_pressure_d1);
    }
    if (status == ATLAS_OK)
    {
        status = AtlasMs5611_Compensate(sensor->prom,
                                        sample->raw_pressure_d1,
                                        sample->raw_temperature_d2,
                                        &sample->pressure_pa,
                                        &sample->temperature_centi_c);
    }
    if (status == ATLAS_OK)
    {
        sample->timestamp_ms = HAL_GetTick();
        ++sensor->health.samples_read;
    }
    return status;
}
