/**
 * @file atlas_board.h
 * @brief Atlas flight-computer module composition, initialization report, and ISR routing.
 *
 * Major functions:
 * - AtlasBoard_Init(): initializes every supported onboard sensor and communication module.
 * - AtlasBoard_Service(): performs bounded foreground protocol work and scheduled output work.
 * - AtlasBoard_LatchRuntimeFault(): preserves a task-detected failure for supervision.
 * - AtlasBoard_AllStartupStepsPassed(): summarizes the complete bring-up report.
 * - HAL GPIO/timer callbacks: route BNO085, LSM6, and GNSS PPS interrupts safely.
 */

#ifndef ATLAS_BOARD_H
#define ATLAS_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "atlas_adxl375.h"
#include "atlas_ble.h"
#include "atlas_bno085.h"
#include "atlas_buzzer.h"
#include "atlas_gnss.h"
#include "atlas_led.h"
#include "atlas_lsm6dsv16b.h"
#include "atlas_mmc5983ma.h"
#include "atlas_ms5611.h"
#include "atlas_rfd900x.h"
#include "atlas_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief HAL handles required by the complete board-support layer. */
typedef struct
{
    I2C_HandleTypeDef *sensor_i2c;
    SPI_HandleTypeDef *shared_sensor_spi;
    SPI_HandleTypeDef *imu_spi;
    UART_HandleTypeDef *gnss_uart;
    UART_HandleTypeDef *radio_uart;
    UART_HandleTypeDef *ble_uart;
    TIM_HandleTypeDef *microsecond_pps_timer;
    TIM_HandleTypeDef *buzzer_timer;
} AtlasBoardHardware;

/** @brief Per-subsystem startup results retained for debugger/telemetry inspection. */
typedef struct
{
    AtlasStatus led;
    AtlasStatus buzzer;
    AtlasStatus adxl375;
    AtlasStatus lsm6dsv16b;
    AtlasStatus mmc5983ma;
    AtlasStatus ms5611;
    AtlasStatus bno085;
    AtlasStatus bno085_default_reports;
    AtlasStatus gnss;
    AtlasStatus gnss_ram_configuration;
    AtlasStatus radio_transport;
    AtlasStatus ble;
} AtlasBoardInitReport;

/** @brief Complete project-owned driver state for one Atlas board. */
typedef struct
{
    AtlasAdxl375 adxl375;
    AtlasLsm6dsv16b lsm6dsv16b;
    AtlasMmc5983ma mmc5983ma;
    AtlasMs5611 ms5611;
    AtlasBno085 bno085;
    AtlasGnss gnss;
    AtlasRfd900x radio;
    AtlasBle ble;
    AtlasBuzzer buzzer;
    AtlasLed led;
    AtlasUartTransport gnss_transport;
    AtlasUartTransport radio_transport;
    AtlasUartTransport ble_transport;
    sh2_SensorValue_t bno085_latest[SH2_MAX_SENSOR_ID + 1U];
    uint64_t bno085_available_mask;
    AtlasBoardInitReport init;
    AtlasStatus runtime_fault;
    uint32_t service_cycles;
    bool init_complete;
} AtlasBoard;

/**
 * @brief Initialize every supported sensor/module and retain every individual result.
 * @param board Destination board instance.
 * @param hardware Initialized STM32 HAL handles matching the Atlas schematic.
 * @return ATLAS_OK when all required bring-up checks pass, otherwise the first failure.
 * @note Initialization continues after a module failure to produce a complete diagnostic report.
 */
AtlasStatus AtlasBoard_Init(AtlasBoard *board, const AtlasBoardHardware *hardware);

/**
 * @brief Service all initialized interrupt-driven transports and scheduled outputs once.
 * @param board Initialized board instance.
 * @return ATLAS_OK when service paths remain healthy, otherwise the first current failure.
 * @note The first runtime service failure is latched so the watchdog supervisor cannot
 *       resume refreshing after a one-cycle protocol/reset fault.
 */
AtlasStatus AtlasBoard_Service(AtlasBoard *board);

/**
 * @brief Latch the first non-OK runtime failure without clearing an earlier cause.
 * @param board Initialized board instance.
 * @param status Runtime status to preserve; ATLAS_OK has no effect.
 * @note Call only from the sole I/O owner after scheduling begins; this preserves
 *       single-writer access to AtlasBoard runtime state.
 */
void AtlasBoard_LatchRuntimeFault(AtlasBoard *board, AtlasStatus status);

/**
 * @brief Report whether every required onboard module passed startup.
 * @param board Board instance with completed initialization.
 * @return true only when all report entries are ATLAS_OK.
 */
bool AtlasBoard_AllStartupStepsPassed(const AtlasBoard *board);

/**
 * @brief Copy the latest decoded BNO085 report for one SH-2 sensor ID.
 * @param board Initialized board instance.
 * @param sensor_id SH-2 sensor/report identifier.
 * @param sample Destination value.
 * @param consume true to clear the availability bit.
 * @return true when at least one report of this type has been received.
 * @note Foreground-only accessor paired with AtlasBoard_Service(), which is the
 *       only writer to the latest-sample cache.
 */
bool AtlasBoard_GetBno085Sample(AtlasBoard *board,
                                sh2_SensorId_t sensor_id,
                                sh2_SensorValue_t *sample,
                                bool consume);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_BOARD_H */
