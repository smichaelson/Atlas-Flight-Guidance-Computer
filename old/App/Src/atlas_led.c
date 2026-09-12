/**
 * @file atlas_led.c
 * @brief Fail-dark RGB channel handling for the hardware-inhibited Atlas rev-0.1 LED.
 *
 * Major functions:
 * - AtlasLed_Init(): reclaims CubeMX TIM4 pin staging and establishes low GPIOs.
 * - AtlasLed_SetRgb(): rejects nonzero requests without ever driving a pin high.
 * - AtlasLed_SetColor(): validates masks and applies the same mandatory inhibit.
 * - AtlasLed_ReadGateMask(): reads back the legacy MCU control-pin levels.
 * - AtlasLed_Off(): provides an unconditional low-write shutdown helper.
 */

#include "atlas_led.h"

#include <string.h>

#if ATLAS_LED_HARDWARE_INHIBITED != 1U
#error "Atlas rev-0.1 RGB outputs must remain hardware-inhibited"
#endif

/** @brief Write every affected MCU/MOSFET channel net low without reading state. */
static void atlas_led_force_low(void)
{
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_RESET);
}

/**
 * @brief Bind the fixed Atlas RGB channels and force the LED off.
 * @param led Destination LED instance.
 * @return ATLAS_OK, ATLAS_ERROR_NULL, or ATLAS_ERROR_IO on low-pin mismatch.
 */
AtlasStatus AtlasLed_Init(AtlasLed *led)
{
    GPIO_InitTypeDef gpio = {0};

    if (led == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    memset(led, 0, sizeof(*led));

    /* Set ODR low before changing away from CubeMX's unused TIM4 alternate function. */
    atlas_led_force_low();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = LED_R_Pin | LED_G_Pin;
    HAL_GPIO_Init(LED_R_GPIO_Port, &gpio);
    gpio.Pin = LED_B_Pin;
    HAL_GPIO_Init(LED_B_GPIO_Port, &gpio);

    led->color = ATLAS_LED_OFF;
    led->output_inhibited = true;
    /* IDR samples the actual MCU control-net level, not merely the requested ODR bit. */
    if (AtlasLed_ReadGateMask(led) != ATLAS_LED_OFF)
    {
        return ATLAS_ERROR_IO;
    }
    led->initialized = true;
    return ATLAS_OK;
}

/**
 * @brief Reject non-off RGB requests and retain verified-low channel nets.
 * @param led Initialized LED instance.
 * @param red true to illuminate red.
 * @param green true to illuminate green.
 * @param blue true to illuminate blue.
 * @return ATLAS_OK for verified off, ATLAS_ERROR_UNSUPPORTED for any on request,
 *         or a typed null/readiness/control-pin-readback failure.
 */
AtlasStatus AtlasLed_SetRgb(AtlasLed *led, bool red, bool green, bool blue)
{
    /* Defense in depth: even malformed, premature, or legacy callers first
     * remove every possible drive before their request is evaluated. */
    atlas_led_force_low();
    if (led == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!led->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }

    led->color = ATLAS_LED_OFF;
    led->output_inhibited = true;
    if (red || green || blue)
    {
        return ATLAS_ERROR_UNSUPPORTED;
    }
    return (AtlasLed_ReadGateMask(led) == ATLAS_LED_OFF) ?
           ATLAS_OK : ATLAS_ERROR_IO;
}

/**
 * @brief Set one enumerated binary RGB color.
 * @param led Initialized LED instance.
 * @param color Valid AtlasLedColor bit combination.
 * @return ATLAS_OK or a typed argument/readiness/control-pin-readback failure.
 */
AtlasStatus AtlasLed_SetColor(AtlasLed *led, AtlasLedColor color)
{
    if (((unsigned)color & ~0x07U) != 0U)
    {
        return ATLAS_ERROR_ARGUMENT;
    }
    return AtlasLed_SetRgb(led,
                           ((unsigned)color & ATLAS_LED_RED) != 0U,
                           ((unsigned)color & ATLAS_LED_GREEN) != 0U,
                           ((unsigned)color & ATLAS_LED_BLUE) != 0U);
}

/**
 * @brief Sample the three legacy MCU control-pin levels as an RGB-compatible mask.
 * @param led LED instance; initialization is not required for diagnostic sampling.
 * @return Bit 0/1/2 for high red/green/blue control nets, or zero for NULL.
 */
uint8_t AtlasLed_ReadGateMask(const AtlasLed *led)
{
    uint8_t mask = 0U;

    if (led == NULL)
    {
        return 0U;
    }
    if (HAL_GPIO_ReadPin(LED_R_GPIO_Port, LED_R_Pin) == GPIO_PIN_SET)
    {
        mask |= ATLAS_LED_RED;
    }
    if (HAL_GPIO_ReadPin(LED_G_GPIO_Port, LED_G_Pin) == GPIO_PIN_SET)
    {
        mask |= ATLAS_LED_GREEN;
    }
    if (HAL_GPIO_ReadPin(LED_B_GPIO_Port, LED_B_Pin) == GPIO_PIN_SET)
    {
        mask |= ATLAS_LED_BLUE;
    }
    return mask;
}

/**
 * @brief Force all RGB channel nets low.
 * @param led LED instance; NULL is ignored after the unconditional low writes.
 */
void AtlasLed_Off(AtlasLed *led)
{
    atlas_led_force_low();
    if (led != NULL)
    {
        led->color = ATLAS_LED_OFF;
        led->output_inhibited = true;
    }
}
