/**
 * @file atlas_analog.c
 * @brief Overflow-safe ADC voltage conversion with per-channel saturation checks.
 * Major functions: AtlasAnalog_Convert applies the rev-0.1 resistor ratios.
 */
#include "atlas_analog.h"
#include <stddef.h>

/** @brief Convert counts through the verified dividers. @param sample Destination.
 * @param raw Counts. @param vdda_mv Measured reference. @param reference_valid Freshness. */
void AtlasAnalog_Convert(AtlasAnalogSample *sample, const uint16_t raw[ATLAS_ANALOG_CHANNELS],
                         uint32_t vdda_mv, bool reference_valid)
{
    /* (Rtop + Rbottom) / Rbottom, reduced exactly; inputs are not ADC-pin mV. */
    static const uint16_t numerator[ATLAS_ANALOG_CHANNELS] = {2U,59U,2U,758U,46U,57U,57U,57U,57U,57U};
    static const uint16_t denominator[ATLAS_ANALOG_CHANNELS] = {1U,19U,1U,83U,5U,10U,10U,10U,10U,10U};
    if (sample == NULL || raw == NULL) return;
    sample->valid_mask = 0U;
    sample->vdda_mv = (vdda_mv <= UINT16_MAX) ? (uint16_t)vdda_mv : 0U;
    const bool valid_reference = reference_valid && vdda_mv >= 2800U && vdda_mv <= 3600U;
    for (uint32_t i = 0; i < ATLAS_ANALOG_CHANNELS; ++i)
    {
        sample->raw[i] = raw[i];
        sample->millivolts[i] = 0U;
        if (valid_reference)
        {
            const uint64_t divisor = UINT64_C(65535) * denominator[i];
            sample->millivolts[i] = (uint32_t)(((uint64_t)raw[i] * vdda_mv * numerator[i] + divisor / 2U) / divisor);
            /* Last 64 codes cannot reliably distinguish a large voltage from clipping. */
            if (raw[i] < 65472U) sample->valid_mask |= (uint16_t)(1U << i);
        }
    }
}
