/**
 * @file atlas_led.c
 * @brief Atlas common-anode RGB LED control through active-high low-side transistors.
 *
 * Major functions:
 * - AtlasLed_Init(): reclaims CubeMX TIM4 pin staging and establishes fail-dark GPIOs.
 * - AtlasLed_SetRgb(): converts logical channel states to GPIO levels.
 * - AtlasLed_SetColor(): validates and applies an RGB bit mask.
 * - AtlasLed_Off(): provides a safe unconditional shutdown helper.
 */

#include "atlas_led.h"

#include <string.h>

/**
 * @brief Bind the fixed Atlas RGB channels and force the LED off.
 * @param led Destination LED instance.
 * @return ATLAS_OK or ATLAS_ERROR_NULL.
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
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_RESET);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = LED_R_Pin | LED_G_Pin;
    HAL_GPIO_Init(LED_R_GPIO_Port, &gpio);
    gpio.Pin = LED_B_Pin;
    HAL_GPIO_Init(LED_B_GPIO_Port, &gpio);

    led->initialized = true;
    led->color = ATLAS_LED_OFF;
    return ATLAS_OK;
}

/**
 * @brief Set the three physical LED channels.
 * @param led Initialized LED instance.
 * @param red true to illuminate red.
 * @param green true to illuminate green.
 * @param blue true to illuminate blue.
 * @return ATLAS_OK or a typed readiness failure.
 */
AtlasStatus AtlasLed_SetRgb(AtlasLed *led, bool red, bool green, bool blue)
{
    if (led == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!led->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }

    /* The MCU drives transistor gates, so high sinks current and lights a die. */
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin,
                      red ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin,
                      green ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin,
                      blue ? GPIO_PIN_SET : GPIO_PIN_RESET);
    led->color = (AtlasLedColor)((red ? ATLAS_LED_RED : 0U) |
                                 (green ? ATLAS_LED_GREEN : 0U) |
                                 (blue ? ATLAS_LED_BLUE : 0U));
    return ATLAS_OK;
}

/**
 * @brief Set one enumerated binary RGB color.
 * @param led Initialized LED instance.
 * @param color Valid AtlasLedColor bit combination.
 * @return ATLAS_OK or a typed argument/readiness failure.
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
 * @brief Turn all RGB channels off.
 * @param led Initialized LED instance; NULL is ignored.
 */
void AtlasLed_Off(AtlasLed *led)
{
    if ((led != NULL) && led->initialized)
    {
        (void)AtlasLed_SetColor(led, ATLAS_LED_OFF);
    }
}
