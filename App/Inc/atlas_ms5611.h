/**
 * @file atlas_ms5611.h
 * @brief MS5611-01BA03 pressure/temperature configuration and compensation firmware.
 *
 * Major functions:
 * - AtlasMs5611_Init(): resets the device, reads PROM, and validates CRC-4.
 * - AtlasMs5611_Read(): performs bounded D2/D1 conversions and second-order compensation.
 * - AtlasMs5611_Crc4()/Compensate(): independently testable data-integrity math.
 */

#ifndef ATLAS_MS5611_H
#define ATLAS_MS5611_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_MS5611_I2C_ADDRESS_7BIT (0x77U)

/** @brief MS5611 oversampling selections. */
typedef enum
{
    ATLAS_MS5611_OSR_256 = 0,
    ATLAS_MS5611_OSR_512,
    ATLAS_MS5611_OSR_1024,
    ATLAS_MS5611_OSR_2048,
    ATLAS_MS5611_OSR_4096
} AtlasMs5611Osr;

/** @brief Compensated pressure and temperature with raw conversion values. */
typedef struct
{
    uint32_t raw_pressure_d1;
    uint32_t raw_temperature_d2;
    int32_t pressure_pa;
    int32_t temperature_centi_c;
    uint32_t timestamp_ms;
} AtlasMs5611Sample;

/** @brief Driver health counters. */
typedef struct
{
    uint32_t io_errors;
    uint32_t prom_crc_failures;
    uint32_t invalid_prom_failures;
    uint32_t samples_read;
} AtlasMs5611Health;

/** @brief MS5611 driver instance. */
typedef struct
{
    I2C_HandleTypeDef *i2c;
    uint16_t address_hal;
    uint16_t prom[8];
    bool initialized;
    AtlasMs5611Health health;
} AtlasMs5611;

/**
 * @brief Reset the Atlas MS5611, read all PROM words, and verify CRC-4.
 * @param sensor Destination driver instance.
 * @param i2c Initialized I2C1 handle at no more than 400 kHz.
 * @return ATLAS_OK or a typed transport, PROM, or CRC failure.
 */
AtlasStatus AtlasMs5611_Init(AtlasMs5611 *sensor, I2C_HandleTypeDef *i2c);

/**
 * @brief Compute the data-sheet CRC-4 nibble for eight PROM words.
 * @param prom Eight PROM words, including the stored CRC in word 7.
 * @return Calculated CRC nibble in bits 3:0; returns 0 for NULL.
 */
uint8_t AtlasMs5611_Crc4(const uint16_t prom[8]);

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
                                   int32_t *temperature_centi_c);

/**
 * @brief Perform temperature and pressure conversions and return compensated data.
 * @param sensor Initialized driver instance.
 * @param osr Oversampling selection applied to both conversions.
 * @param sample Destination sample.
 * @return ATLAS_OK or a typed transport/parameter error.
 * @note This convenience call blocks for at most 20 ms at OSR 4096.
 */
AtlasStatus AtlasMs5611_Read(AtlasMs5611 *sensor,
                             AtlasMs5611Osr osr,
                             AtlasMs5611Sample *sample);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_MS5611_H */
