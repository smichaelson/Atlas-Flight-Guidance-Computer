/** @file test_analog.c
 * @brief Inert rev-0.1 divider, saturation, stale-reference and rounding tests.
 * Major functions: main exercises the production conversion helper. */
#include "atlas_analog.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
/** @brief Execute conversion boundary checks. @return Zero if assertions pass. */
int main(void)
{
    /* Factory and live samples are BOTH 16-bit on STM32H743. The vendor LL
     * macro divides the live sample by 16; exercising that mock hid the bug. */
    assert(AtlasAnalog_VddaFromReference16(24000U, 24000U) == 3300U);
    assert(AtlasAnalog_VddaFromReference16(23860U, 24000U) == 3319U);
    assert(AtlasAnalog_VddaFromReference16(20000U, 24000U) == 3960U);
    assert(AtlasAnalog_VddaFromReference16(0U, 24000U) == 0U);
    assert(AtlasAnalog_VddaFromReference16(65536U, 24000U) == 0U);
    assert(AtlasAnalog_VddaFromReference16(24000U, 0U) == 0U);
    assert(AtlasAnalog_VddaFromReference16(24000U, UINT16_MAX) == 0U);
    assert(AtlasAnalog_VddaFromReference16(1U, 65534U) == 216262200U);
    AtlasAnalogSample sample = {0};
    uint16_t raw[ATLAS_ANALOG_CHANNELS] = {0};
    for (size_t i = 0; i < ATLAS_ANALOG_CHANNELS; ++i) raw[i] = 32768U;
    AtlasAnalog_Convert(&sample, raw, 3300U, true);
    assert(sample.valid_mask == 0x3FFU);
    assert(sample.millivolts[ATLAS_ANALOG_3V3] == 3300U);
    assert(sample.millivolts[ATLAS_ANALOG_PWM_SUPPLY] == 5124U);
    assert(sample.millivolts[ATLAS_ANALOG_VIN_PROTECTED] == 15069U);
    assert(sample.millivolts[ATLAS_ANALOG_ARM_SUPPLY] == 15180U);
    assert(sample.millivolts[ATLAS_ANALOG_CONTINUITY_5] == 9405U);
    raw[0] = UINT16_MAX;
    raw[1] = 65471U;
    raw[2] = 65472U;
    raw[3] = 0U;
    AtlasAnalog_Convert(&sample, raw, 3300U, true);
    assert((sample.valid_mask & 1U) == 0U && (sample.valid_mask & 2U) != 0U);
    assert((sample.valid_mask & 4U) == 0U && sample.millivolts[3] == 0U);
    AtlasAnalog_Convert(&sample, raw, 3300U, false);
    assert(sample.valid_mask == 0U);
    AtlasAnalog_Convert(&sample, raw, UINT32_MAX, true);
    assert(sample.valid_mask == 0U && sample.vdda_mv == 0U);
    AtlasAnalog_Convert(NULL, raw, 3300U, true);
    AtlasAnalog_Convert(&sample, NULL, 3300U, true);
    puts("PASS: ADC rank/divider conversion, clipping, stale reference and numeric boundaries");
    return 0;
}
