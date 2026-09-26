/**
 * @file atlas_stabilization.h
 * @brief Pure gravity estimation and four radial-shaft bench servo geometry.
 * Major functions: Update fuses gyro/acceleration; Calibrate records upright;
 * Targets projects down into each horn plane; Seal/Valid protect SD settings.
 */
#ifndef ATLAS_STABILIZATION_H
#define ATLAS_STABILIZATION_H
#include <stdbool.h>
#include <stdint.h>

#define ATLAS_STAB_CHANNELS 4U
#define ATLAS_STAB_MASK 0xC3U
#define ATLAS_STAB_SAMPLE_MAX_MS 100U
#define ATLAS_STAB_CONFIG_FILE "ASTAB.CFG"
#define ATLAS_STAB_CONFIG_BYTES 64U
#define ATLAS_STAB_COMMAND_PERIOD_MS 5U
#define ATLAS_STAB_COMMAND_WAKE_US 6U
#define ATLAS_STAB_COMMAND_QUIET_US 3U
#define ATLAS_STAB_COMMAND_QUIET_MS 150U

/** @brief Exact estimator/output check, retained at an active-control fault. */
typedef enum
{
    ATLAS_STAB_FAULT_NONE=0, ATLAS_STAB_FAULT_SAMPLE, ATLAS_STAB_FAULT_STALE,
    ATLAS_STAB_FAULT_RATE, ATLAS_STAB_FAULT_ACCEL, ATLAS_STAB_FAULT_VECTOR,
    ATLAS_STAB_FAULT_CORRECTION, ATLAS_STAB_FAULT_SETTLING,
    ATLAS_STAB_FAULT_GEOMETRY, ATLAS_STAB_FAULT_COMMAND, ATLAS_STAB_FAULT_NONFINITE
} AtlasStabilizationFault;

/** @brief Versioned little-endian IEEE-754 SD record, CRC32 over the first 60 bytes. */
typedef struct
{
    uint32_t magic, version, enabled, reverse_mask;
    float upright[3], bias_dps[3];
    uint32_t reserved[5], crc;
} AtlasStabilizationConfig;
/** @brief Latest new direct IMU sample, expressed in the sensor's right-handed axes. */
typedef struct
{
    float accel_g[3], gyro_dps[3];
    uint32_t timestamp_ms;
    bool valid;
} AtlasStabilizationSample;
/** @brief Private estimator state; one owner, no HAL, allocation or time source. */
typedef struct
{
    float up[3], sum_accel[3], sum_gyro[3], previous_accel[3];
    uint32_t timestamp_ms, settled_ms, accel_ms, still_ms, still_count;
    AtlasStabilizationFault fault;
    bool initialized, ready;
} AtlasStabilization;
/** @brief Output diagnostics; channels are ordered PCB 7,8,1,2 throughout. */
typedef struct
{
    uint32_t state, reason, limited_mask, singular_mask, reverse_mask;
    AtlasStabilizationFault fault_detail;
    bool enabled, calibrated, calibration_ready, active, saved, save_busy;
    float up[3];
    uint16_t pulse_us[ATLAS_STAB_CHANNELS];
} AtlasStabilizationSnapshot;
/** @brief Per-shaft rest detection; moving shafts follow every new target directly. */
typedef struct
{
    uint32_t quiet_ms[ATLAS_STAB_CHANNELS];
    uint16_t anchor_us[ATLAS_STAB_CHANNELS];
    uint32_t tracking_mask;
} AtlasStabilizationCommands;

/** @brief Validate schema, CRC, finite calibration and fixed mounting. @param c Record.
 * @return True only for a complete supported record. */
bool AtlasStabilization_ConfigValid(const AtlasStabilizationConfig *c);
/** @brief Stamp schema and checksum after an explicit edit. @param c Record. */
void AtlasStabilization_Seal(AtlasStabilizationConfig *c);
/** @brief Consume new samples and expire retained data. @param s Owner state.
 * @param sample Latest sample. @param c Optional calibrated bias. @param now Current tick. */
void AtlasStabilization_Update(AtlasStabilization *s, const AtlasStabilizationSample *sample,
                               const AtlasStabilizationConfig *c, uint32_t now);
/** @brief Capture one second of stationary upright samples; leaves mode disabled.
 * @param s Estimator. @param reverse Four direction bits. @param c Output record.
 * @return True if stationary, settled and USB-C-up mounting checks pass. */
bool AtlasStabilization_Calibrate(const AtlasStabilization *s, uint32_t reverse,
                                 AtlasStabilizationConfig *c);
/** @brief Project world-down into four independent radial-shaft horn planes.
 * @param c Valid calibration. @param up Estimated up in sensor axes.
 * @param previous Last commanded pulses, used at singularities. @param pulse Output targets.
 * @param limited Four bits for travel clipping. @param singular Four bits for axial gravity.
 * @return False on invalid inputs; no output may be asserted then. */
bool AtlasStabilization_Targets(const AtlasStabilizationConfig *c, const float up[3],
                                const uint16_t previous[4], uint16_t pulse[4],
                                uint32_t *limited, uint32_t *singular);
/** @brief Reset at the actual four-channel 1500 us start. @param s State. @param now Tick. */
void AtlasStabilization_CommandsReset(AtlasStabilizationCommands *s,uint32_t now);
/** @brief Follow direct targets every 5 ms while moving; hold small noise at rest.
 * Wake at 0.6 degrees. Rest after targets remain within 0.3 degrees for 150 ms.
 * @param s Rest state, committed only on success. @param target Geometric targets.
 * @param previous Actual preceding commanded pulses. @param singular Axial-gravity mask.
 * @param now Current tick.
 * @param pulse Output pulses, committed only on success. @return Input validity. */
bool AtlasStabilization_CommandsStep(AtlasStabilizationCommands *s,const uint16_t target[4],
                                     const uint16_t previous[4],uint32_t singular,uint32_t now,uint16_t pulse[4]);
#endif
