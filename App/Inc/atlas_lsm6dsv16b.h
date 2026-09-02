/**
 * @file atlas_lsm6dsv16b.h
 * @brief LSM6DSV16B low-g accelerometer and gyroscope configuration firmware.
 *
 * Major functions:
 * - AtlasLsm6dsv16b_Init(): resets, identifies, configures, and verifies the IMU.
 * - AtlasLsm6dsv16b_ReadSample(): returns coherent temperature, gyro, and accel data.
 * - AtlasLsm6dsv16b_OnInterrupt()/ConsumeInterrupt(): defer INT1 work out of ISR context.
 */

#ifndef ATLAS_LSM6DSV16B_H
#define ATLAS_LSM6DSV16B_H

#include <stdbool.h>
#include <stdint.h>

#include "atlas_spi_device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_LSM6DSV16B_WHO_AM_I (0x71U)

/** @brief Supported high-performance accelerometer/gyroscope ODR codes. */
typedef enum
{
    ATLAS_LSM6_ODR_POWER_DOWN = 0x00,
    ATLAS_LSM6_ODR_60_HZ      = 0x05,
    ATLAS_LSM6_ODR_120_HZ     = 0x06,
    ATLAS_LSM6_ODR_240_HZ     = 0x07,
    ATLAS_LSM6_ODR_480_HZ     = 0x08,
    ATLAS_LSM6_ODR_960_HZ     = 0x09
} AtlasLsm6dsv16bOdr;

/** @brief Accelerometer full-scale codes from CTRL8.FS_XL. */
typedef enum
{
    ATLAS_LSM6_ACCEL_2_G  = 0x00,
    ATLAS_LSM6_ACCEL_4_G  = 0x01,
    ATLAS_LSM6_ACCEL_8_G  = 0x02,
    ATLAS_LSM6_ACCEL_16_G = 0x03
} AtlasLsm6dsv16bAccelRange;

/** @brief Gyroscope full-scale codes from CTRL6.FS_G. */
typedef enum
{
    ATLAS_LSM6_GYRO_125_DPS  = 0x00,
    ATLAS_LSM6_GYRO_250_DPS  = 0x01,
    ATLAS_LSM6_GYRO_500_DPS  = 0x02,
    ATLAS_LSM6_GYRO_1000_DPS = 0x03,
    ATLAS_LSM6_GYRO_2000_DPS = 0x04,
    ATLAS_LSM6_GYRO_4000_DPS = 0x0C
} AtlasLsm6dsv16bGyroRange;

/** @brief Configurable baseline for the direct IMU data path. */
typedef struct
{
    AtlasLsm6dsv16bOdr accel_odr;
    AtlasLsm6dsv16bOdr gyro_odr;
    AtlasLsm6dsv16bAccelRange accel_range;
    AtlasLsm6dsv16bGyroRange gyro_range;
    bool route_data_ready_to_int1;
} AtlasLsm6dsv16bConfig;

/** @brief One coherent direct-register IMU sample. */
typedef struct
{
    int16_t raw_temperature;
    int16_t raw_gyro_x;
    int16_t raw_gyro_y;
    int16_t raw_gyro_z;
    int16_t raw_accel_x;
    int16_t raw_accel_y;
    int16_t raw_accel_z;
    float temperature_c;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    uint32_t timestamp_ms;
} AtlasLsm6dsv16bSample;

/** @brief Driver health counters. */
typedef struct
{
    uint32_t io_errors;
    uint32_t identity_failures;
    uint32_t configuration_mismatches;
    uint32_t reset_timeouts;
    volatile uint32_t interrupt_count;
    uint32_t samples_read;
} AtlasLsm6dsv16bHealth;

/** @brief LSM6DSV16B driver instance. */
typedef struct
{
    AtlasSpiDevice device;
    AtlasLsm6dsv16bConfig config;
    volatile uint32_t interrupt_count;
    uint32_t interrupt_count_consumed;
    bool initialized;
    AtlasLsm6dsv16bHealth health;
} AtlasLsm6dsv16b;

/**
 * @brief Return the reviewed Atlas default: 240 Hz, +/-16 g, +/-2000 dps, INT1 DRDY.
 * @return Complete default configuration.
 */
AtlasLsm6dsv16bConfig AtlasLsm6dsv16b_DefaultConfig(void);

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
                                 const AtlasLsm6dsv16bConfig *config);

/**
 * @brief Read STATUS_REG and report whether both accel and gyro have fresh data.
 * @param sensor Initialized driver instance.
 * @param ready Destination readiness flag.
 * @return ATLAS_OK or a typed transport error.
 */
AtlasStatus AtlasLsm6dsv16b_DataReady(AtlasLsm6dsv16b *sensor, bool *ready);

/**
 * @brief Burst-read and scale temperature, gyro XYZ, and accel XYZ.
 * @param sensor Initialized driver instance.
 * @param sample Destination sample.
 * @return ATLAS_OK, ATLAS_ERROR_NOT_READY, or a typed transport error.
 */
AtlasStatus AtlasLsm6dsv16b_ReadSample(AtlasLsm6dsv16b *sensor,
                                       AtlasLsm6dsv16bSample *sample);

/**
 * @brief Record an INT1 edge; safe to call from HAL_GPIO_EXTI_Callback.
 * @param sensor Initialized driver instance.
 */
void AtlasLsm6dsv16b_OnInterrupt(AtlasLsm6dsv16b *sensor);

/**
 * @brief Consume coalesced INT1 activity in foreground code.
 * @param sensor Initialized driver instance.
 * @return true when one or more new edges occurred since the prior call.
 */
bool AtlasLsm6dsv16b_ConsumeInterrupt(AtlasLsm6dsv16b *sensor);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_LSM6DSV16B_H */
