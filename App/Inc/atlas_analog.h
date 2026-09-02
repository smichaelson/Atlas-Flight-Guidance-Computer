/**
 * @file atlas_analog.h
 * @brief Rev-0.1 ADC rank names, engineering units and validity rules.
 * Major functions: AtlasAnalog_Convert converts one coherent ten-rank scan.
 * Ratios follow the exported schematic, not a substitute for measured calibration.
 */
#ifndef ATLAS_ANALOG_H
#define ATLAS_ANALOG_H
#include <stdbool.h>
#include <stdint.h>
#define ATLAS_ANALOG_CHANNELS (10U)
/** @brief Indices match ADC1's generated regular sequence exactly. */
typedef enum
{
    ATLAS_ANALOG_3V3 = 0, ATLAS_ANALOG_PWM_SUPPLY, ATLAS_ANALOG_5V,
    ATLAS_ANALOG_VIN_PROTECTED, ATLAS_ANALOG_ARM_SUPPLY,
    ATLAS_ANALOG_CONTINUITY_1, ATLAS_ANALOG_CONTINUITY_2,
    ATLAS_ANALOG_CONTINUITY_3, ATLAS_ANALOG_CONTINUITY_4,
    ATLAS_ANALOG_CONTINUITY_5
} AtlasAnalogChannel;
/** @brief Sample validity bits refer to millivolts[]; invalid numbers must not authorize outputs. */
typedef struct
{
    uint32_t sequence, sampled_at_ms, reference_at_ms;
    uint32_t millivolts[ATLAS_ANALOG_CHANNELS];
    uint16_t raw[ATLAS_ANALOG_CHANNELS];
    uint16_t valid_mask, vdda_mv;
    int16_t die_temperature_c;
} AtlasAnalogSample;
/** @brief Convert raw 16-bit ranks using a measured, factory-calibrated reference.
 * @param sample Destination; timestamps/sequence are retained.
 * @param raw Ten raw ADC1 values, in generated rank order.
 * @param vdda_mv Measured VDDA, 2800-3600 mV required.
 * @param reference_valid True only for a fresh, checked ADC3 reference reading.
 * @note Saturation near the ADC rail invalidates that channel; 0 remains a valid
 *       voltage (but does not prove continuity or a physical arm connection). */
void AtlasAnalog_Convert(AtlasAnalogSample *sample, const uint16_t raw[ATLAS_ANALOG_CHANNELS],
                         uint32_t vdda_mv, bool reference_valid);
#endif
