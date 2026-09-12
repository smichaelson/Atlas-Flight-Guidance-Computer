/**
 * @file atlas_adxl375.h
 * @brief ADXL375 high-g accelerometer configuration and sampled-data interface.
 *
 * Major functions:
 * - AtlasAdxl375_Init(): verifies DEVID, configures 4-wire SPI, ODR, and measurement mode.
 * - AtlasAdxl375_ReadSample(): performs one coherent six-byte XYZ burst read.
 * - AtlasAdxl375_SetMeasurementEnabled(): moves explicitly between standby and measurement.
 */

#ifndef ATLAS_ADXL375_H
#define ATLAS_ADXL375_H

#include <stdbool.h>
#include <stdint.h>

#include "atlas_spi_device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_ADXL375_DEVICE_ID (0xE5U)
#define ATLAS_ADXL375_SCALE_G_PER_LSB (0.049F)

/** @brief ADXL375 normal-power output-data-rate register codes. */
typedef enum
{
    ATLAS_ADXL375_ODR_25_HZ   = 0x08,
    ATLAS_ADXL375_ODR_50_HZ   = 0x09,
    ATLAS_ADXL375_ODR_100_HZ  = 0x0A,
    ATLAS_ADXL375_ODR_200_HZ  = 0x0B,
    ATLAS_ADXL375_ODR_400_HZ  = 0x0C,
    ATLAS_ADXL375_ODR_800_HZ  = 0x0D,
    ATLAS_ADXL375_ODR_1600_HZ = 0x0E,
    ATLAS_ADXL375_ODR_3200_HZ = 0x0F
} AtlasAdxl375Odr;

/** @brief Raw and engineering-unit acceleration from one coherent sample. */
typedef struct
{
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;
    float x_g;
    float y_g;
    float z_g;
    uint32_t timestamp_ms;
} AtlasAdxl375Sample;

/** @brief Driver diagnostics retained across transactions. */
typedef struct
{
    uint32_t io_errors;
    uint32_t identity_failures;
    uint32_t configuration_mismatches;
    uint32_t samples_read;
} AtlasAdxl375Health;

/** @brief ADXL375 driver instance. */
typedef struct
{
    AtlasSpiDevice device;
    AtlasAdxl375Odr odr;
    bool initialized;
    AtlasAdxl375Health health;
} AtlasAdxl375;

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
                              AtlasAdxl375Odr odr);

/**
 * @brief Enable measurement mode or return the sensor to standby.
 * @param sensor Initialized driver instance.
 * @param enabled true for measurement; false for standby.
 * @return ATLAS_OK or a typed transport/configuration failure.
 */
AtlasStatus AtlasAdxl375_SetMeasurementEnabled(AtlasAdxl375 *sensor, bool enabled);

/**
 * @brief Check the DATA_READY source bit without changing configuration.
 * @param sensor Initialized driver instance.
 * @param ready Destination for DATA_READY state.
 * @return ATLAS_OK or a typed transport failure.
 */
AtlasStatus AtlasAdxl375_DataReady(AtlasAdxl375 *sensor, bool *ready);

/**
 * @brief Read one coherent XYZ sample and convert it using the 49 mg/LSB nominal scale.
 * @param sensor Initialized driver instance.
 * @param sample Destination sample.
 * @return ATLAS_OK or a typed transport failure.
 */
AtlasStatus AtlasAdxl375_ReadSample(AtlasAdxl375 *sensor,
                                    AtlasAdxl375Sample *sample);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_ADXL375_H */
