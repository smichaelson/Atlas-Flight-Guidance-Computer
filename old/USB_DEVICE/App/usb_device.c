/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the USB Device
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
 * Atlas USB library setup. Major functions:
 * - AtlasUsbDevice_Init(): initializes the library but DOES NOT connect the pull-up.
 * - MX_USB_DEVICE_Init(): compatibility setup wrapper; use AtlasUsb_Start() in applications.
 * The AtlasUsb task owns start/stop and gates connection using the PA9 VBUS divider.
 */
#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
USBD_HandleTypeDef hUsbDeviceFS;

/** @brief Prepare CDC while disconnected; optional USB failure is nonfatal.
 * @return USB status; caller must tear down on error and separately gate USBD_Start. */
uint8_t AtlasUsbDevice_Init(void)
{
    HAL_PWREx_EnableUSBVoltageDetector();
    if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK ||
        USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC) != USBD_OK ||
        USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS) != USBD_OK)
        return USBD_FAIL;
    return USBD_OK;
}
/** @brief Compatibility library setup only; ordinary boot uses the AtlasUsb owner. */
void MX_USB_DEVICE_Init(void)
{
    (void)AtlasUsbDevice_Init();
}
