/**
 * @file stm32h7xx_hal.h
 * @brief SD-only HAL boundary for production polling BSP/FatFs acceptance.
 * Major definitions: configuration, detect pin, reset trace and I/O hooks.
 */
#ifndef ATLAS_REVIEW_SD_HAL_H
#define ATLAS_REVIEW_SD_HAL_H
#include "main.h"
#define SDMMC_BUS_WIDE_1B 1U
#define SDMMC_BUS_WIDE_4B 4U
#define SDMMC_CLOCK_EDGE_RISING 0U
#define SDMMC_CLOCK_POWER_SAVE_DISABLE 0U
#define SDMMC_HARDWARE_FLOW_CONTROL_ENABLE 1U
#define HAL_SD_CARD_TRANSFER 4U
#define HAL_SD_CARD_SENDING 5U
#define HAL_SD_CARD_RECEIVING 6U
#define HAL_SD_CARD_PROGRAMMING 7U
#define SDMMC1_IRQn 1U
extern uint32_t review_sd_instance;
#define SDMMC1 (&review_sd_instance)
#define SD_DET_Pin 8U
#define SD_DET_GPIO_Port (&atlas_test_gpio_d)
#define __DSB() do {} while (0)
void AtlasReview_SdReset(void);
#define __HAL_RCC_SDMMC1_FORCE_RESET() AtlasReview_SdReset()
#define __HAL_RCC_SDMMC1_RELEASE_RESET() do {} while (0)
#define HAL_NVIC_DisableIRQ(irq) ((void)(irq))
#define HAL_NVIC_ClearPendingIRQ(irq) ((void)(irq))
typedef struct
{
    void *Instance;
    struct { uint32_t ClockEdge, ClockPowerSave, BusWide, HardwareFlowControl, ClockDiv; } Init;
} SD_HandleTypeDef;
typedef struct { uint32_t LogBlockNbr, LogBlockSize; } HAL_SD_CardInfoTypeDef;
HAL_StatusTypeDef HAL_SD_Init(SD_HandleTypeDef *sd);
HAL_StatusTypeDef HAL_SD_ConfigWideBusOperation(SD_HandleTypeDef *sd, uint32_t width);
HAL_StatusTypeDef HAL_SD_ReadBlocks(SD_HandleTypeDef *sd, uint8_t *data,
                                   uint32_t address, uint32_t count, uint32_t timeout);
HAL_StatusTypeDef HAL_SD_WriteBlocks(SD_HandleTypeDef *sd, uint8_t *data,
                                    uint32_t address, uint32_t count, uint32_t timeout);
uint32_t HAL_SD_GetCardState(SD_HandleTypeDef *sd);
HAL_StatusTypeDef HAL_SD_GetCardInfo(SD_HandleTypeDef *sd, HAL_SD_CardInfoTypeDef *info);
#endif
