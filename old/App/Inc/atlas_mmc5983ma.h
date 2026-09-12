/**
 * @file atlas_mmc5983ma.h
 * @brief MMC5983MA 18-bit magnetometer configuration and compensated measurements.
 *
 * Major functions:
 * - AtlasMmc5983ma_Init(): software-resets, identifies, and verifies bandwidth setup.
 * - AtlasMmc5983ma_ReadField(): performs a bounded Auto-SET/RESET single measurement.
 * - AtlasMmc5983ma_ReadFieldSetReset(): cancels bridge offset with a paired measurement.
 * - AtlasMmc5983ma_ReadTemperature(): triggers and converts the internal temperature ADC.
 */

#ifndef ATLAS_MMC5983MA_H
#define ATLAS_MMC5983MA_H

#include <stdbool.h>
#include <stdint.h>

#include "atlas_spi_device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_MMC5983MA_PRODUCT_ID (0x30U)
#define ATLAS_MMC5983MA_COUNTS_PER_GAUSS (16384.0F)
#define ATLAS_MMC5983MA_ZERO_FIELD_COUNTS (131072L)

/** @brief Measurement bandwidth/latency codes from Internal Control 1. */
typedef enum
{
    ATLAS_MMC5983_BW_100_HZ_8_MS = 0x00,
    ATLAS_MMC5983_BW_200_HZ_4_MS = 0x01,
    ATLAS_MMC5983_BW_400_HZ_2_MS = 0x02,
    ATLAS_MMC5983_BW_800_HZ_0_5_MS = 0x03
} AtlasMmc5983maBandwidth;

/** @brief One raw 18-bit XYZ conversion. */
typedef struct
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
} AtlasMmc5983maRaw;

/** @brief Magnetic field in gauss and the raw source conversion. */
typedef struct
{
    AtlasMmc5983maRaw raw;
    float x_gauss;
    float y_gauss;
    float z_gauss;
    uint32_t timestamp_ms;
} AtlasMmc5983maField;

/** @brief Driver health counters. */
typedef struct
{
    uint32_t io_errors;
    uint32_t identity_failures;
    uint32_t configuration_mismatches;
    uint32_t measurement_timeouts;
    uint32_t measurements;
} AtlasMmc5983maHealth;

/** @brief MMC5983MA driver instance. */
typedef struct
{
    AtlasSpiDevice device;
    AtlasMmc5983maBandwidth bandwidth;
    bool initialized;
    AtlasMmc5983maHealth health;
} AtlasMmc5983ma;

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
                                AtlasMmc5983maBandwidth bandwidth);

/**
 * @brief Decode the sensor's packed seven-byte 18-bit XYZ representation.
 * @param bytes Seven bytes from registers 0x00 through 0x06.
 * @param raw Destination raw values.
 * @return ATLAS_OK or a parameter error.
 */
AtlasStatus AtlasMmc5983ma_UnpackRaw(const uint8_t bytes[7],
                                     AtlasMmc5983maRaw *raw);

/**
 * @brief Trigger one magnetic conversion with automatic bridge SET/RESET.
 * @param sensor Initialized driver instance.
 * @param field Destination field sample.
 * @param timeout_ms Nonzero bounded completion timeout.
 * @return ATLAS_OK or a typed timeout/transport failure.
 */
AtlasStatus AtlasMmc5983ma_ReadField(AtlasMmc5983ma *sensor,
                                     AtlasMmc5983maField *field,
                                     uint32_t timeout_ms);

/**
 * @brief Perform explicit SET and RESET conversions and cancel bridge offset.
 * @param sensor Initialized driver instance.
 * @param field Destination compensated field; raw contains the SET reading.
 * @param timeout_ms Nonzero timeout applied independently to each conversion.
 * @return ATLAS_OK or a typed timeout/transport failure.
 */
AtlasStatus AtlasMmc5983ma_ReadFieldSetReset(AtlasMmc5983ma *sensor,
                                             AtlasMmc5983maField *field,
                                             uint32_t timeout_ms);

/**
 * @brief Trigger and read the internal temperature measurement.
 * @param sensor Initialized driver instance.
 * @param temperature_c Destination temperature in degrees Celsius.
 * @param timeout_ms Nonzero bounded completion timeout.
 * @return ATLAS_OK or a typed timeout/transport failure.
 */
AtlasStatus AtlasMmc5983ma_ReadTemperature(AtlasMmc5983ma *sensor,
                                           float *temperature_c,
                                           uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_MMC5983MA_H */
