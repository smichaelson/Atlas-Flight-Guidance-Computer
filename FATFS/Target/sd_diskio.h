/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sd_diskio.h
  * @brief   Header for sd_diskio.c module
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

/* Atlas uses polling in one storage task; preserve this port during regeneration. */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SD_DISKIO_H
#define __SD_DISKIO_H

/* USER CODE BEGIN firstSection */
/* can be used to modify / undefine following code or add new definitions */
/* USER CODE END firstSection */

/* Includes ------------------------------------------------------------------*/
#include "bsp_driver_sd.h"
#include "ff_gen_drv.h"
/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
extern const Diskio_drvTypeDef  SD_Driver;
/** @brief Invalidate/reset the optional medium; storage-owner context only. */
void SD_Invalidate(void);
/** @brief Authorize exactly one initialization as part of an explicit owner mount.
 * @note Never call from a file read/write or from another task. */
void SD_PrepareMount(void);

/* USER CODE BEGIN lastSection */
/* can be used to modify / undefine previous code or add new definitions */
/* USER CODE END lastSection */

#endif /* __SD_DISKIO_H */
