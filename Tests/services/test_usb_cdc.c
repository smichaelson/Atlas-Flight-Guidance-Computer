/**
 * @file test_usb_cdc.c
 * @brief Exercise the production CDC interface and ST core/class, without USB hardware.
 * Major functions: main enumerates a modeled device; control_cases tests real EP0
 * routing/stalls; data_cases tests retained buffers, ZLP completion and teardown.
 * This LL model does not validate electrical behavior, FIFO timing or a host driver.
 */
#include "usbd_core.h"
#include "usbd_cdc_if.h"
#include "atlas_usb.h"
#include <assert.h>
#include <stdio.h>

USBD_HandleTypeDef hUsbDeviceFS;
uint32_t test_primask, test_ipsr;
static PCD_HandleTypeDef pcd;
static union { uint64_t align; uint8_t bytes[2048]; } allocation;
static bool allocated, configured, fail_transmit, expect_rx_copy;
static uint8_t *tx_data[16], *rx_data[16], retained_rx[64];
static uint32_t tx_length[16], rx_length[16], rx_actual[16];
static unsigned tx_calls[16], stalls, class_starts, class_stops, rx_calls;
static uint32_t completed;
static uint16_t control_lines;
static bool stalled[256];

void TestSetPrimask(uint32_t mask) { test_primask=mask; }
void TestUsbDelay(uint32_t ms) { (void)ms; }
void USBD_LL_Delay(uint32_t ms) { (void)ms; }
void *USBD_static_malloc(uint32_t size)
{ assert(!allocated && size<=sizeof(allocation));allocated=true;return allocation.bytes; }
void USBD_static_free(void *data) { assert(data==allocation.bytes && allocated);allocated=false; }
void AtlasUsb_OnClassState(bool state)
{ configured=state;if(state)++class_starts;else ++class_stops; }
void AtlasUsb_OnReceive(const uint8_t *data,uint32_t length)
{ assert(length<=sizeof(retained_rx));memcpy(retained_rx,data,length);++rx_calls; }
void AtlasUsb_OnTransmit(uint32_t length) { completed+=length; }
void AtlasUsb_OnControlLines(uint16_t value) { control_lines=value; }

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *dev) { dev->pData=&pcd;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *dev) { (void)dev;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *dev) { (void)dev;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *dev) { (void)dev;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *dev,uint8_t address,uint8_t type,uint16_t size)
{
    (void)dev;(void)type;
    if(address&0x80U)pcd.IN_ep[address&15U].maxpacket=size;
    else pcd.OUT_ep[address&15U].maxpacket=size;
    return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *dev,uint8_t address)
{ (void)dev;(void)address;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *dev,uint8_t address)
{ (void)dev;(void)address;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *dev,uint8_t address)
{ (void)dev;stalled[address]=true;++stalls;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *dev,uint8_t address)
{ (void)dev;stalled[address]=false;return USBD_OK; }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *dev,uint8_t address)
{ (void)dev;return stalled[address]?1U:0U; }
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *dev,uint8_t address)
{ (void)dev;(void)address;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *dev,uint8_t address,uint8_t *data,uint32_t size)
{
    (void)dev;
    if(fail_transmit)return USBD_FAIL;
    const unsigned ep=address&15U;
    /* Retain, do not copy: the real non-DMA PCD reads the bytes in a later IRQ. */
    tx_data[ep]=data;tx_length[ep]=size;++tx_calls[ep];
    return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *dev,uint8_t address,uint8_t *data,uint32_t size)
{
    (void)dev;const unsigned ep=address&15U;
    if(ep==1U && expect_rx_copy)assert(rx_calls!=0U && memcmp(retained_rx,data,rx_actual[ep])==0);
    rx_data[ep]=data;rx_length[ep]=size;return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *dev,uint8_t address)
{ (void)dev;return rx_actual[address&15U]; }

/** @brief Deliver one real setup packet; clear modeled EP0 stall as hardware does.
 * @param type Direction/type/recipient. @param request Opcode. @param value wValue.
 * @param index Interface/endpoint. @param length wLength. @return Core result. */
static USBD_StatusTypeDef setup(uint8_t type,uint8_t request,uint16_t value,uint16_t index,uint16_t length)
{
    uint8_t bytes[8]={type,request,(uint8_t)value,(uint8_t)(value>>8),
        (uint8_t)index,(uint8_t)(index>>8),(uint8_t)length,(uint8_t)(length>>8)};
    stalls=tx_calls[0]=0U;stalled[0]=stalled[0x80]=false;
    return USBD_LL_SetupStage(&hUsbDeviceFS,bytes);
}
/** @brief Send an EP0 OUT payload through core completion handling.
 * @param data Payload. @param length Actual bytes (may be shorter than requested).
 * @return Core result. */
static USBD_StatusTypeDef control_out(const uint8_t *data,uint32_t length)
{
    assert(length<=rx_length[0] && rx_data[0]!=NULL);
    memcpy(rx_data[0],data,length);rx_actual[0]=length;
    return USBD_LL_DataOutStage(&hUsbDeviceFS,0U,rx_data[0]);
}
/** @brief Test ACM request validation, short payloads and status-data lifetime. */
static void control_cases(void)
{
    const uint8_t default_line[7]={0x00,0xC2,0x01,0x00,0U,0U,8U};
    const uint8_t next_line[7]={0x80,0x25,0x00,0x00,0U,0U,8U}; /* 9600 metadata. */
    assert(setup(0xA1U,CDC_GET_LINE_CODING,0U,0U,7U)==USBD_OK);
    assert(tx_length[0]==7U && memcmp(tx_data[0],default_line,7U)==0 && stalls==0U);
    assert(setup(0x21U,CDC_SET_LINE_CODING,0U,0U,7U)==USBD_OK && tx_calls[0]==0U);
    assert(control_out(next_line,7U)==USBD_OK && tx_calls[0]==1U && tx_length[0]==0U);
    assert(setup(0xA1U,CDC_GET_LINE_CODING,0U,0U,7U)==USBD_OK);
    assert(memcmp(tx_data[0],next_line,7U)==0);
    assert(setup(0x21U,CDC_SET_CONTROL_LINE_STATE,3U,0U,0U)==USBD_OK);
    assert(control_lines==3U && tx_calls[0]==1U && stalls==0U);

    uint8_t bad[7];memcpy(bad,next_line,7U);bad[4]=3U;
    assert(setup(0x21U,CDC_SET_LINE_CODING,0U,0U,7U)==USBD_OK);
    assert(control_out(bad,7U)==USBD_FAIL && stalls!=0U && tx_calls[0]==0U);
    assert(setup(0x21U,CDC_SET_LINE_CODING,0U,0U,7U)==USBD_OK);
    assert(control_out(next_line,3U)==USBD_FAIL && stalls!=0U && tx_calls[0]==0U);
    assert(setup(0xA1U,CDC_GET_LINE_CODING,0U,0U,7U)==USBD_OK);
    assert(memcmp(tx_data[0],next_line,7U)==0); /* Neither malformed write was committed. */

    const struct { uint8_t type,request;uint16_t value,index,length; } invalid[]={
        {0xA1,CDC_GET_LINE_CODING,0,0,64}, {0xA1,CDC_GET_LINE_CODING,0,0,6},
        {0x21,CDC_GET_LINE_CODING,0,0,7}, {0xA1,CDC_SET_LINE_CODING,0,0,7},
        {0x21,CDC_SET_LINE_CODING,0,0,0}, {0x21,CDC_SET_LINE_CODING,0,0,65535},
        {0x21,CDC_SET_CONTROL_LINE_STATE,4,0,0}, {0x21,CDC_SET_CONTROL_LINE_STATE,0,1,0},
        {0xA1,CDC_GET_LINE_CODING,0,0x100,7}, {0xA1,CDC_GET_LINE_CODING,0,2,7},
        {0xA1,0xFE,0,0,7}, {0x21,0xFE,0,0,0}
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
    {
        (void)setup(invalid[i].type,invalid[i].request,invalid[i].value,invalid[i].index,invalid[i].length);
        assert(stalls!=0U && tx_calls[0]==0U);
    }
    assert(setup(0x81U,USB_REQ_GET_INTERFACE,0U,1U,1U)==USBD_OK);
    assert(tx_length[0]==1U && tx_data[0][0]==0U);
    assert(setup(0x81U,USB_REQ_GET_STATUS,0U,1U,2U)==USBD_OK);
    assert(tx_length[0]==2U && tx_data[0][0]==0U && tx_data[0][1]==0U);
    assert(setup(0x82U,USB_REQ_GET_STATUS,0U,CDC_IN_EP,2U)==USBD_OK);
    assert(tx_length[0]==2U && tx_data[0][0]==0U && tx_data[0][1]==0U);
    stalled[CDC_IN_EP]=true;
    assert(setup(0x82U,USB_REQ_GET_STATUS,0U,CDC_IN_EP,2U)==USBD_OK);
    assert(tx_data[0][0]==1U && tx_data[0][1]==0U); /* Reserved high byte cannot alias is_used. */
    stalled[CDC_IN_EP]=false;
}
/** @brief Test private TX/RX lifetime, rejected launch, 64-byte ZLP and reset. */
static void data_cases(void)
{
    uint8_t source[65];memset(source,0x5AU,sizeof(source));
    assert(CDC_Transmit_FS(NULL,1U)==USBD_FAIL && CDC_Transmit_FS(source,0U)==USBD_FAIL);
    assert(CDC_Transmit_FS(source,65U)==USBD_FAIL);
    fail_transmit=true;
    assert(CDC_Transmit_FS(source,1U)==USBD_FAIL && !CDC_TransmitBusy_FS());
    fail_transmit=false;
    assert(CDC_Transmit_FS(source,64U)==USBD_OK && CDC_TransmitBusy_FS());
    assert(tx_data[1]!=source && tx_length[1]==64U);
    memset(source,0xC3U,sizeof(source));assert(tx_data[1][0]==0x5AU);
    assert(CDC_Transmit_FS(source,1U)==USBD_BUSY && tx_data[1][0]==0x5AU);
    unsigned calls=tx_calls[1];
    assert(USBD_LL_DataInStage(&hUsbDeviceFS,1U,tx_data[1])==USBD_OK);
    assert(CDC_TransmitBusy_FS() && completed==0U && tx_calls[1]==calls+1U && tx_length[1]==0U);
    assert(USBD_LL_DataInStage(&hUsbDeviceFS,1U,NULL)==USBD_OK);
    assert(!CDC_TransmitBusy_FS() && completed==64U);
    assert(CDC_Transmit_FS(source,3U)==USBD_OK);
    assert(USBD_LL_DataInStage(&hUsbDeviceFS,1U,tx_data[1])==USBD_OK);
    assert(completed==67U && !CDC_TransmitBusy_FS());

    assert(rx_length[1]==64U);memcpy(rx_data[1],"ATLAS",5U);rx_actual[1]=5U;expect_rx_copy=true;
    assert(USBD_LL_DataOutStage(&hUsbDeviceFS,1U,rx_data[1])==USBD_OK);
    assert(rx_calls==1U && memcmp(retained_rx,"ATLAS",5U)==0);
    expect_rx_copy=false;
    assert(CDC_Transmit_FS(source,7U)==USBD_OK);
    assert(USBD_LL_Reset(&hUsbDeviceFS)==USBD_OK);
    assert(!configured && !allocated && class_stops==1U);
    assert(!CDC_TransmitBusy_FS() && CDC_Transmit_FS(source,1U)==USBD_FAIL);
    assert(completed==67U); /* Reset is not a delivery acknowledgement. */
}
/** @brief Run deterministic USB integration checks. @return Zero when all pass. */
int main(void)
{
    USBD_DescriptorsTypeDef descriptors={0};
    uint8_t byte=0U;
    assert(CDC_Transmit_FS(&byte,1U)==USBD_FAIL && !CDC_TransmitBusy_FS());
    assert(USBD_Init(&hUsbDeviceFS,&descriptors,DEVICE_FS)==USBD_OK);
    assert(USBD_RegisterClass(&hUsbDeviceFS,&USBD_CDC)==USBD_OK);
    assert(USBD_CDC_RegisterInterface(&hUsbDeviceFS,&USBD_Interface_fops_FS)==USBD_OK);
    assert(USBD_LL_SetSpeed(&hUsbDeviceFS,USBD_SPEED_FULL)==USBD_OK);
    assert(USBD_LL_Reset(&hUsbDeviceFS)==USBD_OK);
    assert(setup(0U,USB_REQ_SET_ADDRESS,1U,0U,0U)==USBD_OK);
    assert(setup(0U,USB_REQ_SET_CONFIGURATION,1U,0U,0U)==USBD_OK);
    assert(configured && class_starts==1U && hUsbDeviceFS.dev_state==USBD_STATE_CONFIGURED);
    assert(pcd.IN_ep[1].maxpacket==64U && pcd.IN_ep[2].maxpacket==CDC_CMD_PACKET_SIZE);
    control_cases();data_cases();
    assert(USBD_DeInit(&hUsbDeviceFS)==USBD_OK);
    puts("USB CDC: real core/class control validation, stalls, retained buffers, ZLP and reset PASS");
    return 0;
}
