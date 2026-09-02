/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    bsp_driver_sd.c for H7 (based on stm32h743i_eval_sd.c)
 * @brief   This file includes a generic uSD card driver.
 *          To be completed by the user according to the board used for the project.
 * @note    Some functions generated as weak: they can be overridden by
 *          - code in user files
 *          - or BSP code from the FW pack files
 *          if such files are added to the generated project (by the user).
 ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
 ******************************************************************************
 */
/* USER CODE END Header */

/**
 * Atlas-owned replacement for the generated SD scaffold.
 * Major functions: BSP_SD_Init() configures/probes optional media;
 * BSP_SD_ReadBlocks()/WriteBlocks() perform bounded synchronous transfers;
 * BSP_SD_DeInit() resets the controller after removal/error.
 * Only the storage task may call these functions. IDMA is deliberately unused.
 */
#include "bsp_driver_sd.h"
#include "main.h"
#include "atlas_time.h"
#include <string.h>

extern SD_HandleTypeDef hsd1;
static uint8_t sd_initialized;
static volatile uint32_t media_generation;
static uint32_t mounted_generation;

/** @brief Record either detect-switch edge; a replacement invalidates the old FAT. */
void BSP_SD_DetectFromISR(void)
{
    ++media_generation;
}

/** @brief Reset the optional controller and discard stale software state. */
void BSP_SD_DeInit(void)
{
    /* HAL_SD_Abort can return before disabling IDMA on an error. Peripheral
       reset, not a successful-looking handle state, establishes quiescence. */
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __DSB();
    __HAL_RCC_SDMMC1_RELEASE_RESET();
    HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
    memset(&hsd1, 0, sizeof(hsd1));
    sd_initialized = 0U;
}

/** @brief Configure and probe a card without making absence fatal. @return MSD status. */
uint8_t BSP_SD_Init(void)
{
    BSP_SD_DeInit();
    const uint32_t generation = media_generation;
    if (BSP_SD_IsDetected() != SD_PRESENT) return MSD_ERROR_SD_NOT_PRESENT;
    AtlasTime_DelayMs(20U);
    if (BSP_SD_IsDetected() != SD_PRESENT || media_generation != generation)
        return MSD_ERROR_SD_NOT_PRESENT;
    mounted_generation = generation;
    hsd1.Instance = SDMMC1;
    hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    /* Identification is single-bit. Change card AND host width only afterward. */
    hsd1.Init.BusWide = SDMMC_BUS_WIDE_1B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hsd1.Init.ClockDiv = 1U; /* PLL2R 50 MHz / (2 * 1) = 25 MHz transfer clock. */
    if ((HAL_SD_Init(&hsd1) != HAL_OK) ||
        (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK))
    {
        BSP_SD_DeInit();
        return MSD_ERROR;
    }
    /* Polling owns completion; no SD ISR may consume its flags. */
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    if (media_generation != mounted_generation || BSP_SD_IsDetected() != SD_PRESENT)
    {
        BSP_SD_DeInit();
        return MSD_ERROR_SD_NOT_PRESENT;
    }
    sd_initialized = 1U;
    return MSD_OK;
}

/**
 * @brief Read sectors synchronously; caller memory is CPU-accessed only.
 * @param data Destination. @param address LBA. @param count Sector count.
 * @param timeout_ms HAL transfer bound. @return MSD status.
 */
uint8_t BSP_SD_ReadBlocks(uint32_t *data, uint32_t address,
                         uint32_t count, uint32_t timeout_ms)
{
    if (!sd_initialized || data == NULL || count == 0U || timeout_ms == 0U ||
        media_generation != mounted_generation || BSP_SD_IsDetected() != SD_PRESENT)
        return MSD_ERROR;
    return HAL_SD_ReadBlocks(&hsd1, (uint8_t *)data, address, count,
                             timeout_ms) == HAL_OK &&
           media_generation == mounted_generation &&
           BSP_SD_IsDetected() == SD_PRESENT ? MSD_OK : MSD_ERROR;
}

/**
 * @brief Write sectors synchronously; never retains the source.
 * @param data Source. @param address LBA. @param count Sector count.
 * @param timeout_ms HAL transfer bound. @return MSD status.
 */
uint8_t BSP_SD_WriteBlocks(uint32_t *data, uint32_t address,
                          uint32_t count, uint32_t timeout_ms)
{
    if (!sd_initialized || data == NULL || count == 0U || timeout_ms == 0U ||
        media_generation != mounted_generation || BSP_SD_IsDetected() != SD_PRESENT)
        return MSD_ERROR;
    return HAL_SD_WriteBlocks(&hsd1, (uint8_t *)data, address, count,
                              timeout_ms) == HAL_OK &&
           media_generation == mounted_generation &&
           BSP_SD_IsDetected() == SD_PRESENT ? MSD_OK : MSD_ERROR;
}

/** @brief Probe protocol readiness, not mechanical presence. @return Transfer state. */
uint8_t BSP_SD_GetCardState(void)
{
    uint32_t state;
    if (!sd_initialized || media_generation != mounted_generation ||
        BSP_SD_IsDetected() != SD_PRESENT) return SD_TRANSFER_ERROR;
    state = HAL_SD_GetCardState(&hsd1);
    if (!BSP_SD_IsMediaCurrent()) return SD_TRANSFER_ERROR;
    if (state == HAL_SD_CARD_TRANSFER) return SD_TRANSFER_OK;
    if ((state == HAL_SD_CARD_SENDING) || (state == HAL_SD_CARD_RECEIVING) ||
        (state == HAL_SD_CARD_PROGRAMMING)) return SD_TRANSFER_BUSY;
    return SD_TRANSFER_ERROR;
}

/** @brief Obtain validated geometry. @param info Destination. @return MSD status. */
uint8_t BSP_SD_GetCardInfo(BSP_SD_CardInfo *info)
{
    if (!BSP_SD_IsMediaCurrent() || info == NULL) return MSD_ERROR;
    return HAL_SD_GetCardInfo(&hsd1, info) == HAL_OK && BSP_SD_IsMediaCurrent() ? MSD_OK : MSD_ERROR;
}

/** @brief Read J3 DET via PD3/R21 (active low). @return SD_PRESENT or SD_NOT_PRESENT. */
uint8_t BSP_SD_IsDetected(void)
{
    return HAL_GPIO_ReadPin(SD_DET_GPIO_Port, SD_DET_Pin) == GPIO_PIN_RESET ?
           SD_PRESENT : SD_NOT_PRESENT;
}
/** @brief Detect invalidated/removable-media sessions without an SD bus transaction.
 * @return One for the same initialized and physically present card. */
uint8_t BSP_SD_IsMediaCurrent(void)
{
    return sd_initialized && mounted_generation == media_generation && BSP_SD_IsDetected() == SD_PRESENT;
}
