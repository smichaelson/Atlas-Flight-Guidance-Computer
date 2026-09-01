/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define GPIO_IN1_Pin GPIO_PIN_2
#define GPIO_IN1_GPIO_Port GPIOE
#define GPIO_IN2_Pin GPIO_PIN_3
#define GPIO_IN2_GPIO_Port GPIOE
#define GPIO_OUT2_Pin GPIO_PIN_4
#define GPIO_OUT2_GPIO_Port GPIOE
#define BUZZER_PWM_L_Pin GPIO_PIN_5
#define BUZZER_PWM_L_GPIO_Port GPIOE
#define BUZZER_PWM_H_Pin GPIO_PIN_6
#define BUZZER_PWM_H_GPIO_Port GPIOE
#define GPIO_OUT1_Pin GPIO_PIN_2
#define GPIO_OUT1_GPIO_Port GPIOF
#define SENSE_8V4_Pin GPIO_PIN_0
#define SENSE_8V4_GPIO_Port GPIOC
#define SENSE_5V_SYS_Pin GPIO_PIN_1
#define SENSE_5V_SYS_GPIO_Port GPIOC
#define SENSE_3V3_SYS_Pin GPIO_PIN_0
#define SENSE_3V3_SYS_GPIO_Port GPIOA
#define PYRO_ARMED_SENSE_Pin GPIO_PIN_1
#define PYRO_ARMED_SENSE_GPIO_Port GPIOA
#define PYRO_CONT1_Pin GPIO_PIN_2
#define PYRO_CONT1_GPIO_Port GPIOA
#define PYRO_CONT2_Pin GPIO_PIN_3
#define PYRO_CONT2_GPIO_Port GPIOA
#define PYRO_CONT3_Pin GPIO_PIN_4
#define PYRO_CONT3_GPIO_Port GPIOA
#define GPIO_OUT3_Pin GPIO_PIN_5
#define GPIO_OUT3_GPIO_Port GPIOA
#define PYRO_CONT4_Pin GPIO_PIN_6
#define PYRO_CONT4_GPIO_Port GPIOA
#define GPIO_OUT4_Pin GPIO_PIN_7
#define GPIO_OUT4_GPIO_Port GPIOA
#define PYRO_CONT5_Pin GPIO_PIN_4
#define PYRO_CONT5_GPIO_Port GPIOC
#define GPIO_OUT5_Pin GPIO_PIN_5
#define GPIO_OUT5_GPIO_Port GPIOC
#define PWM7_Pin GPIO_PIN_0
#define PWM7_GPIO_Port GPIOB
#define PWM8_Pin GPIO_PIN_1
#define PWM8_GPIO_Port GPIOB
#define SENSE_VIN_PROT_Pin GPIO_PIN_11
#define SENSE_VIN_PROT_GPIO_Port GPIOF
#define EXT_SWITCH_Pin GPIO_PIN_12
#define EXT_SWITCH_GPIO_Port GPIOF
#define EXT_SWITCH_EXTI_IRQn EXTI15_10_IRQn
#define GPIO_OUT7_Pin GPIO_PIN_13
#define GPIO_OUT7_GPIO_Port GPIOF
#define GPIO_OUT6_Pin GPIO_PIN_14
#define GPIO_OUT6_GPIO_Port GPIOF
#define BNO085_H_INTN_Pin GPIO_PIN_0
#define BNO085_H_INTN_GPIO_Port GPIOG
#define BNO085_H_INTN_EXTI_IRQn EXTI0_IRQn
#define GPIO_IN3_Pin GPIO_PIN_1
#define GPIO_IN3_GPIO_Port GPIOG
#define GPIO_IN4_Pin GPIO_PIN_7
#define GPIO_IN4_GPIO_Port GPIOE
#define GPIO_IN5_Pin GPIO_PIN_8
#define GPIO_IN5_GPIO_Port GPIOE
#define PWM1_Pin GPIO_PIN_9
#define PWM1_GPIO_Port GPIOE
#define GPIO_IN6_Pin GPIO_PIN_10
#define GPIO_IN6_GPIO_Port GPIOE
#define PWM2_Pin GPIO_PIN_11
#define PWM2_GPIO_Port GPIOE
#define PWM3_Pin GPIO_PIN_13
#define PWM3_GPIO_Port GPIOE
#define PWM4_Pin GPIO_PIN_14
#define PWM4_GPIO_Port GPIOE
#define GPIO_IN7_Pin GPIO_PIN_15
#define GPIO_IN7_GPIO_Port GPIOE
#define LORA_RX_Pin GPIO_PIN_11
#define LORA_RX_GPIO_Port GPIOB
#define BNO085_NRST_Pin GPIO_PIN_13
#define BNO085_NRST_GPIO_Port GPIOB
#define GNSS_TX_Pin GPIO_PIN_14
#define GNSS_TX_GPIO_Port GPIOB
#define GNSS_RX_Pin GPIO_PIN_15
#define GNSS_RX_GPIO_Port GPIOB
#define LORA_TX_Pin GPIO_PIN_8
#define LORA_TX_GPIO_Port GPIOD
#define PYRO_FIRE1_Pin GPIO_PIN_9
#define PYRO_FIRE1_GPIO_Port GPIOD
#define PYRO_FIRE2_Pin GPIO_PIN_10
#define PYRO_FIRE2_GPIO_Port GPIOD
#define PYRO_FIRE3_Pin GPIO_PIN_11
#define PYRO_FIRE3_GPIO_Port GPIOD
#define PYRO_FIRE4_Pin GPIO_PIN_12
#define PYRO_FIRE4_GPIO_Port GPIOD
#define PYRO_FIRE5_Pin GPIO_PIN_13
#define PYRO_FIRE5_GPIO_Port GPIOD
#define LED_B_Pin GPIO_PIN_14
#define LED_B_GPIO_Port GPIOD
#define LSM6_INT1_Pin GPIO_PIN_2
#define LSM6_INT1_GPIO_Port GPIOG
#define LSM6_INT1_EXTI_IRQn EXTI2_IRQn
#define BLE_SWITCH2_Pin GPIO_PIN_4
#define BLE_SWITCH2_GPIO_Port GPIOG
#define BLE_SWITCH1_Pin GPIO_PIN_5
#define BLE_SWITCH1_GPIO_Port GPIOG
#define BLE_DSR_Pin GPIO_PIN_6
#define BLE_DSR_GPIO_Port GPIOG
#define BLE_RTS_Pin GPIO_PIN_8
#define BLE_RTS_GPIO_Port GPIOG
#define PWM5_Pin GPIO_PIN_6
#define PWM5_GPIO_Port GPIOC
#define PWM6_Pin GPIO_PIN_7
#define PWM6_GPIO_Port GPIOC
#define USB_VBUS_Pin GPIO_PIN_9
#define USB_VBUS_GPIO_Port GPIOA
#define USB_VBUS_EXTI_IRQn EXTI9_5_IRQn
#define GNSS_PPS_Pin GPIO_PIN_15
#define GNSS_PPS_GPIO_Port GPIOA
#define EXT1_RX_Pin GPIO_PIN_0
#define EXT1_RX_GPIO_Port GPIOD
#define EXT1_TX_Pin GPIO_PIN_1
#define EXT1_TX_GPIO_Port GPIOD
#define SD_DET_Pin GPIO_PIN_3
#define SD_DET_GPIO_Port GPIOD
#define SD_DET_EXTI_IRQn EXTI3_IRQn
#define BLE_RESET_N_Pin GPIO_PIN_4
#define BLE_RESET_N_GPIO_Port GPIOD
#define BLE_RX_Pin GPIO_PIN_9
#define BLE_RX_GPIO_Port GPIOG
#define CS_LSM6DSV16B_Pin GPIO_PIN_10
#define CS_LSM6DSV16B_GPIO_Port GPIOG
#define CS_MMC5983_Pin GPIO_PIN_11
#define CS_MMC5983_GPIO_Port GPIOG
#define CS_ADXL375_Pin GPIO_PIN_12
#define CS_ADXL375_GPIO_Port GPIOG
#define CS_SPI_EXT_Pin GPIO_PIN_13
#define CS_SPI_EXT_GPIO_Port GPIOG
#define BLE_TX_Pin GPIO_PIN_14
#define BLE_TX_GPIO_Port GPIOG
#define BLE_CTS_Pin GPIO_PIN_15
#define BLE_CTS_GPIO_Port GPIOG
#define LED_R_Pin GPIO_PIN_6
#define LED_R_GPIO_Port GPIOB
#define LED_G_Pin GPIO_PIN_7
#define LED_G_GPIO_Port GPIOB
#define BLE_DTR_Pin GPIO_PIN_1
#define BLE_DTR_GPIO_Port GPIOE
#define BLE_DTR_EXTI_IRQn EXTI1_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
