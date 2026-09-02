/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    bsp_driver_sd.h (based on stm32h743i_eval_sd.h)
  * @brief   This file contains the common defines and functions prototypes for
  *          the bsp_driver_sd.c driver.
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
 * Atlas polling SD port. Major functions: initialize/deinitialize, read/write,
 * readiness, geometry and J3/PD3 card detection. No DMA or erase API is exposed.
 */
#ifndef ATLAS_BSP_SD_H
#define ATLAS_BSP_SD_H
#include "stm32h7xx_hal.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BSP_SD_CardInfo HAL_SD_CardInfoTypeDef
#define MSD_OK                 ((uint8_t)0U)
#define MSD_ERROR              ((uint8_t)1U)
#define MSD_ERROR_SD_NOT_PRESENT ((uint8_t)2U)
#define SD_TRANSFER_OK         ((uint8_t)0U)
#define SD_TRANSFER_BUSY       ((uint8_t)1U)
#define SD_TRANSFER_ERROR      ((uint8_t)2U)
#define SD_PRESENT             ((uint8_t)1U)
#define SD_NOT_PRESENT         ((uint8_t)0U)
/** @brief Probe/configure optional media. @return MSD status. */
uint8_t BSP_SD_Init(void);
/** @brief Reset SDMMC1 and invalidate the handle; storage-owner context only. */
void BSP_SD_DeInit(void);
/** @brief Read synchronously. @param data Destination. @param address LBA.
 * @param count Sectors. @param timeout_ms HAL bound. @return MSD status. */
uint8_t BSP_SD_ReadBlocks(uint32_t *data, uint32_t address, uint32_t count, uint32_t timeout_ms);
/** @brief Write synchronously. @param data Source. @param address LBA.
 * @param count Sectors. @param timeout_ms HAL bound. @return MSD status. */
uint8_t BSP_SD_WriteBlocks(uint32_t *data, uint32_t address, uint32_t count, uint32_t timeout_ms);
/** @brief Probe card protocol state. @return SD_TRANSFER_* value. */
uint8_t BSP_SD_GetCardState(void);
/** @brief Read card geometry. @param info Destination. @return MSD status. */
uint8_t BSP_SD_GetCardInfo(BSP_SD_CardInfo *info);
/** @brief Read raw J3 DET (active low); readiness is separate. @return Presence. */
uint8_t BSP_SD_IsDetected(void);
/** @brief Check initialized media generation/presence without sending a command.
 * @return One only while the mounted card session remains current. */
uint8_t BSP_SD_IsMediaCurrent(void);
/** @brief Record either PD3 detect edge; ISR-only, no HAL/RTOS calls. */
void BSP_SD_DetectFromISR(void);
#ifdef __cplusplus
}
#endif
#endif
