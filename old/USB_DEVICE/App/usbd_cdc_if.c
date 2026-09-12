/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v1.0_Cube
  * @brief          : Usb device for Virtual Com Port.
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
 * Atlas CDC adapter. Major functions:
 * - CDC_Init_FS()/DeInit_FS(): reset session and buffer state.
 * - CDC_Control_FS(): validated seven-byte line coding and DTR/RTS.
 * - CDC_Receive_FS(): retain RX before rearming the endpoint.
 * - CDC_Transmit_FS(): copy TX into private storage until the completion callback.
 */
#include "usbd_cdc_if.h"
#include "atlas_usb.h"
#include <string.h>
#include <stdbool.h>
extern USBD_HandleTypeDef hUsbDeviceFS;
static uint8_t rx_buffer[CDC_DATA_FS_MAX_PACKET_SIZE];
static uint8_t tx_buffer[ATLAS_USB_PACKET_CAPACITY];
static uint8_t line_coding[7] = {0x00, 0xC2, 0x01, 0x00, 0, 0, 8}; /* 115200 8N1. */

/** @brief Allocate no memory and initialize the class's retained buffers. @return USB result. */
static int8_t CDC_Init_FS(void)
{
    if (USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_buffer, 0U) != USBD_OK ||
        USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_buffer) != USBD_OK) return USBD_FAIL;
    AtlasUsb_OnClassState(true);
    return USBD_OK;
}
/** @brief Invalidate all old-session work. @return USB result. */
static int8_t CDC_DeInit_FS(void)
{
    AtlasUsb_OnClassState(false);
    return USBD_OK;
}
/** @brief Validate CDC requests; baud is metadata, never another UART's setting.
 * @param cmd Request. @param data Payload or setup structure for length zero.
 * @param length Payload size. @return USBD_FAIL requests an EP0 stall. */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *data, uint16_t length)
{
    if (data == NULL) return USBD_FAIL;
    switch (cmd)
    {
        case CDC_SET_LINE_CODING:
            if (length != 7U || data[4] > 2U || data[5] > 4U ||
                !((data[6] >= 5U && data[6] <= 8U) || data[6] == 16U)) return USBD_FAIL;
            memcpy(line_coding, data, sizeof(line_coding));
            return USBD_OK;
        case CDC_GET_LINE_CODING:
            if (length != 7U) return USBD_FAIL;
            memcpy(data, line_coding, sizeof(line_coding));
            return USBD_OK;
        case CDC_SET_CONTROL_LINE_STATE:
            if (length != 0U) return USBD_FAIL;
            AtlasUsb_OnControlLines(((USBD_SetupReqTypedef *)data)->wValue);
            return USBD_OK;
        case CDC_SEND_BREAK: /* Accepted as metadata; it does not drive a physical pin. */
            return length == 0U ? USBD_OK : USBD_FAIL;
        default: return USBD_FAIL;
    }
}
/** @brief Retain an OUT packet before allowing another. @param data Received bytes.
 * @param length Count pointer. @return USB result. */
static int8_t CDC_Receive_FS(uint8_t *data, uint32_t *length)
{
    if (data == NULL || length == NULL || *length > sizeof(rx_buffer)) return USBD_FAIL;
    AtlasUsb_OnReceive(data, *length);
    if (USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_buffer) != USBD_OK) return USBD_FAIL;
    return (int8_t)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
}
/** @brief Safely query current class TX state. @return true only while configured and busy. */
bool CDC_TransmitBusy_FS(void)
{
    const USBD_CDC_HandleTypeDef *cdc = hUsbDeviceFS.pClassDataCmsit[hUsbDeviceFS.classId];
    return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && cdc != NULL && cdc->TxState != 0U;
}
/** @brief Copy and start a TX; caller bytes can be reused on return.
 * @param data Source. @param length 1-64 bytes. @return USB status.
 * @note The AtlasUsb owner calls this; endpoint lifetime is held in tx_buffer. */
uint8_t CDC_Transmit_FS(uint8_t *data, uint16_t length)
{
    const uint32_t mask = __get_PRIMASK();
    uint8_t result = USBD_FAIL;
    __disable_irq();
    USBD_CDC_HandleTypeDef *cdc = hUsbDeviceFS.pClassDataCmsit[hUsbDeviceFS.classId];
    if (data != NULL && length != 0U && length <= sizeof(tx_buffer) &&
        hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && cdc != NULL)
    {
        if (cdc->TxState != 0U) result = USBD_BUSY;
        else
        {
            memcpy(tx_buffer, data, length);
            result = USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_buffer, length);
            if (result == USBD_OK) result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
        }
    }
    __DMB();
    __set_PRIMASK(mask);
    return result;
}
/** @brief Record completion after the final data/ZLP handshake. @param data Retained buffer.
 * @param length Completed bytes. @param ep Endpoint. @return USB result. */
static int8_t CDC_TransmitCplt_FS(uint8_t *data, uint32_t *length, uint8_t ep)
{
    (void)data; (void)ep;
    if (length != NULL) AtlasUsb_OnTransmit(*length);
    return USBD_OK;
}
USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
    CDC_Init_FS, CDC_DeInit_FS, CDC_Control_FS, CDC_Receive_FS, CDC_TransmitCplt_FS
};
