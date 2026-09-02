/**
 * @file atlas_led.h
 * @brief Active-high transistor-driven RGB status LED firmware for Atlas.
 *
 * Major functions:
 * - AtlasLed_Init(): takes GPIO ownership from TIM4 staging and forces a known off state.
 * - AtlasLed_SetRgb(): controls each physical color channel explicitly.
 * - AtlasLed_SetColor(): selects one of the eight binary RGB combinations.
 * - AtlasLed_Off(): removes drive from all three low-side transistors.
 */

#ifndef ATLAS_LED_H
#define ATLAS_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Binary RGB colors supported by the GPIO-driven LED. */
typedef enum
{
    ATLAS_LED_OFF = 0x00,
    ATLAS_LED_RED = 0x01,
    ATLAS_LED_GREEN = 0x02,
    ATLAS_LED_BLUE = 0x04,
    ATLAS_LED_YELLOW = 0x03,
    ATLAS_LED_MAGENTA = 0x05,
    ATLAS_LED_CYAN = 0x06,
    ATLAS_LED_WHITE = 0x07
} AtlasLedColor;

/** @brief RGB LED state. */
typedef struct
{
    AtlasLedColor color;
    bool initialized;
} AtlasLed;

/**
 * @brief Bind the fixed Atlas RGB channels and force the LED off.
 * @param led Destination LED instance.
 * @return ATLAS_OK or ATLAS_ERROR_NULL.
 * @note Reconfigures PB6, PB7, and PD14 from CubeMX TIM4 staging to GPIO outputs.
 */
AtlasStatus AtlasLed_Init(AtlasLed *led);

/**
 * @brief Set the three physical LED channels.
 * @param led Initialized LED instance.
 * @param red true to illuminate red.
 * @param green true to illuminate green.
 * @param blue true to illuminate blue.
 * @return ATLAS_OK or a typed readiness failure.
 * @note MCU-high turns a channel on despite the package being common-anode.
 */
AtlasStatus AtlasLed_SetRgb(AtlasLed *led, bool red, bool green, bool blue);

/**
 * @brief Set one enumerated binary RGB color.
 * @param led Initialized LED instance.
 * @param color Valid AtlasLedColor bit combination.
 * @return ATLAS_OK or a typed argument/readiness failure.
 */
AtlasStatus AtlasLed_SetColor(AtlasLed *led, AtlasLedColor color);

/**
 * @brief Turn all RGB channels off.
 * @param led Initialized LED instance; NULL is ignored.
 */
void AtlasLed_Off(AtlasLed *led);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_LED_H */
