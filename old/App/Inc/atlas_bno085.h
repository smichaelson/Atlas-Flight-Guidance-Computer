/**
 * @file atlas_bno085.h
 * @brief BNO085 I2C/SHTP transport and CEVA SH-2 integration for Atlas.
 *
 * Major functions:
 * - AtlasBno085_Init(): resets, opens SH-2, and verifies product identification.
 * - AtlasBno085_EnableReport(): enables a typed SH-2 sensor report at a fixed interval.
 * - AtlasBno085_Service(): drains bounded SHTP traffic and dispatches decoded samples.
 * - AtlasBno085_OnInterrupt(): records the active-low BNO085 interrupt in ISR context.
 */

#ifndef ATLAS_BNO085_H
#define ATLAS_BNO085_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"
#include "sh2.h"
#include "sh2_SensorValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Seven-bit I2C address selected by the Atlas rev-0.1 SA0 strap.
 * @note U12 pin 17 (SA0/H_MOSI) is tied to ground, selecting 0x4A. STM32 HAL
 *       master calls receive the left-shifted address value 0x94.
 */
#define ATLAS_BNO085_I2C_ADDRESS_7BIT (0x4AU)
#define ATLAS_BNO085_MAX_SERVICE_READS (8U)

/** @brief Callback receiving one successfully decoded SH-2 sensor value. */
typedef void (*AtlasBno085SampleCallback)(void *context,
                                          const sh2_SensorValue_t *sample);

/** @brief Stage of the most recent retained BNO085 transport failure. */
typedef enum
{
    ATLAS_BNO085_FAILURE_NONE = 0,
    ATLAS_BNO085_FAILURE_READ_HEADER,
    ATLAS_BNO085_FAILURE_READ_TRANSFER,
    ATLAS_BNO085_FAILURE_WRITE_TRANSFER,
    ATLAS_BNO085_FAILURE_HEADER_LENGTH,
    ATLAS_BNO085_FAILURE_TRANSFER_LENGTH,
    ATLAS_BNO085_FAILURE_I2C_DEINIT,
    ATLAS_BNO085_FAILURE_I2C_INIT,
    ATLAS_BNO085_FAILURE_ANALOG_FILTER,
    ATLAS_BNO085_FAILURE_DIGITAL_FILTER
} AtlasBno085FailureStage;

/** @brief Driver counters for transport and report diagnostics. */
typedef struct
{
    volatile uint32_t interrupts;
    uint32_t transfers_read;
    uint32_t transfers_written;
    uint32_t io_errors;
    uint32_t protocol_errors;
    uint32_t decoded_samples;
    uint32_t decode_errors;
    uint32_t async_resets;
    uint32_t bus_recovery_attempts;
    uint32_t bus_recovery_failures;
    uint32_t last_hal_status;
    uint32_t last_hal_error;
    uint32_t last_failure_stage;
    uint32_t last_transfer_length;
} AtlasBno085Health;

/** @brief BNO085 driver instance; the SH-2 HAL must remain the first member. */
typedef struct
{
    sh2_Hal_t hal;
    I2C_HandleTypeDef *i2c;
    TIM_HandleTypeDef *microsecond_timer;
    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
    GPIO_TypeDef *interrupt_port;
    uint16_t interrupt_pin;
    uint16_t address_hal;
    volatile bool interrupt_pending;
    volatile uint32_t interrupt_timestamp_us;
    volatile bool interrupt_rearmed;
    uint32_t pending_timestamp_us;
    uint16_t pending_transfer_length;
    bool transport_failed;
    bool session_open;
    bool initialized;
    AtlasBno085SampleCallback sample_callback;
    void *sample_context;
    sh2_ProductIds_t product_ids;
    AtlasBno085Health health;
} AtlasBno085;

/**
 * @brief Reset the BNO085, open the official CEVA SH-2 stack, and read product IDs.
 * @param sensor Destination driver instance.
 * @param i2c Initialized I2C1 handle at no more than 400 kHz.
 * @param microsecond_timer Free-running 1 MHz TIM2 handle.
 * @param sample_callback Optional callback for decoded sensor samples.
 * @param sample_context Opaque value passed to sample_callback.
 * @return ATLAS_OK or a typed parameter, transport, timeout, or identity failure.
 * @note The CEVA SH-2 implementation is a singleton; initialize only one BNO085.
 */
AtlasStatus AtlasBno085_Init(AtlasBno085 *sensor,
                             I2C_HandleTypeDef *i2c,
                             TIM_HandleTypeDef *microsecond_timer,
                             AtlasBno085SampleCallback sample_callback,
                             void *sample_context);

/**
 * @brief Enable or update one SH-2 report.
 * @param sensor Initialized BNO085 instance.
 * @param sensor_id SH-2 report identifier, such as SH2_ROTATION_VECTOR.
 * @param report_interval_us Nonzero interval between reports in microseconds.
 * @param batch_interval_us Batch latency in microseconds; zero disables batching.
 * @return ATLAS_OK or a typed parameter/SH-2 communication failure.
 */
AtlasStatus AtlasBno085_EnableReport(AtlasBno085 *sensor,
                                     sh2_SensorId_t sensor_id,
                                     uint32_t report_interval_us,
                                     uint32_t batch_interval_us);

/**
 * @brief Disable one SH-2 report.
 * @param sensor Initialized BNO085 instance.
 * @param sensor_id SH-2 report identifier to disable.
 * @return ATLAS_OK or a typed parameter/SH-2 communication failure.
 */
AtlasStatus AtlasBno085_DisableReport(AtlasBno085 *sensor,
                                      sh2_SensorId_t sensor_id);

/**
 * @brief Service at most ATLAS_BNO085_MAX_SERVICE_READS pending transfers.
 * @param sensor Initialized BNO085 instance.
 * @return ATLAS_OK or a typed readiness, transport, protocol, or reset-state failure.
 * @note An asynchronous hub reset returns ATLAS_ERROR_STATE because enabled reports
 *       must be configured again before sensor data can be trusted.
 */
AtlasStatus AtlasBno085_Service(AtlasBno085 *sensor);

/**
 * @brief Record the BNO085 active-low interrupt without performing bus I/O.
 * @param sensor Driver instance; NULL is ignored.
 * @note Call from HAL_GPIO_EXTI_Callback() for BNO085_H_INTN_Pin.
 */
void AtlasBno085_OnInterrupt(AtlasBno085 *sensor);

/**
 * @brief Close SH-2 and hold the BNO085 in reset.
 * @param sensor Driver instance; NULL is ignored.
 */
void AtlasBno085_Deinit(AtlasBno085 *sensor);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_BNO085_H */
