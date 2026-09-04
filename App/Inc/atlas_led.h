/**
 * @file atlas_led.h
 * @brief Hardware-inhibited RGB LED interface for Atlas rev-0.1.
 *
 * Major functions:
 * - AtlasLed_Init(): takes GPIO ownership from TIM4 staging and forces all channels low.
 * - AtlasLed_SetRgb(): rejects every non-off request while preserving the fail-dark state.
 * - AtlasLed_SetColor(): validates a mask and routes it through the hardware inhibit.
 * - AtlasLed_ReadGateMask(): samples the three legacy MCU control nets for diagnostics.
 * - AtlasLed_Off(): unconditionally writes all three channel nets low.
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

/**
 * @brief Compile-time safety boundary for the as-built rev-0.1 Q6-Q8 mismatch.
 * @note Do not set this to zero. Re-enabling RGB output requires qualified PCB
 *       rework/revision evidence and an explicit firmware safety review.
 */
#define ATLAS_LED_HARDWARE_INHIBITED (1U)

/** @brief Legacy binary RGB request encodings; only OFF is supported on rev-0.1. */
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
    bool output_inhibited;
} AtlasLed;

/**
 * @brief Bind the fixed Atlas RGB channel nets and force them all low.
 * @param led Destination LED instance.
 * @return ATLAS_OK, ATLAS_ERROR_NULL, or ATLAS_ERROR_IO on low-pin mismatch.
 * @note Reconfigures PB6, PB7, and PD14 from CubeMX TIM4 staging to GPIO outputs.
 */
AtlasStatus AtlasLed_Init(AtlasLed *led);

/**
 * @brief Request three physical LED channels through the mandatory inhibit.
 * @param led Initialized LED instance.
 * @param red true to illuminate red.
 * @param green true to illuminate green.
 * @param blue true to illuminate blue.
 * @return ATLAS_OK only for an off request with verified-low pins;
 *         ATLAS_ERROR_UNSUPPORTED for any on request, or another typed failure.
 * @note This API never writes a channel high on the affected PCB revision.
 */
AtlasStatus AtlasLed_SetRgb(AtlasLed *led, bool red, bool green, bool blue);

/**
 * @brief Request one enumerated RGB mask through the mandatory inhibit.
 * @param led Initialized LED instance.
 * @param color Valid AtlasLedColor bit combination.
 * @return ATLAS_OK only for ATLAS_LED_OFF with verified-low pins;
 *         ATLAS_ERROR_UNSUPPORTED for a valid nonzero color.
 */
AtlasStatus AtlasLed_SetColor(AtlasLed *led, AtlasLedColor color);

/**
 * @brief Read the electrical logic level at all three legacy MCU control pins.
 * @param led LED instance; initialization is not required for diagnostic sampling.
 * @return AtlasLedColor-compatible high-level mask, or zero for NULL.
 * @note On rev-0.1 these nets reach the MOSFETs' physical source terminals, not
 *       their gates. Readback cannot prove 5V_SYS, drain current, or emitted light.
 */
uint8_t AtlasLed_ReadGateMask(const AtlasLed *led);

/**
 * @brief Force all RGB channel nets low, regardless of initialization state.
 * @param led LED instance; NULL is ignored after the unconditional low writes.
 */
void AtlasLed_Off(AtlasLed *led);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_LED_H */
