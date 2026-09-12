/** @file test_usb_owner.c @brief Script the real USB owner through lifecycle faults.
 * Major functions: fixture binds modeled VBUS/library boundaries; stream_cases
 * checks copied queues/rings; scripts test loss during init, unplug and TX timeout.
 * The real CDC/core is exercised separately in test_usb_cdc.c. No real bus is used. */
#include "service_model.h"
#include "../../App/Src/atlas_usb.c"
#include <assert.h>
#include <stdio.h>
USBD_HandleTypeDef hUsbDeviceFS;
static unsigned inits,attaches,detaches,pullup_drops,tx_launches;
static bool lose_during_init,busy;
static uint8_t retained_tx[64];
static uint16_t retained_length;

uint8_t AtlasUsbDevice_Init(void)
{
    assert(test_primask==0U);++inits;test_tick+=7U;
    if(lose_during_init){GPIOA->IDR=0U;AtlasUsb_OnVbusEdge();}
    return USBD_OK;
}
USBD_StatusTypeDef USBD_Start(USBD_HandleTypeDef *dev)
{ (void)dev;assert(usb_vbus() && !vbus_lost);++attaches;AtlasUsb_OnClassState(true);return USBD_OK; }
USBD_StatusTypeDef USBD_DeInit(USBD_HandleTypeDef *dev)
{ (void)dev;assert(test_primask==0U);++detaches;busy=false;AtlasUsb_OnClassState(false);return USBD_OK; }
HAL_StatusTypeDef USB_DevDisconnect(const void *instance)
{ assert(instance==USB_OTG_FS);++pullup_drops;return HAL_OK; }
void HAL_NVIC_DisableIRQ(int irq) { assert(irq==OTG_FS_IRQn); }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq==OTG_FS_IRQn && test_primask==0U); }
bool CDC_TransmitBusy_FS(void) { return busy; }
uint8_t CDC_Transmit_FS(uint8_t *data,uint16_t length)
{
    if(!health.configured)return USBD_FAIL;
    if(busy)return USBD_BUSY;
    assert(length<=sizeof(retained_tx));memcpy(retained_tx,data,length);retained_length=length;
    busy=true;++tx_launches;return USBD_OK;
}
/** @brief Reset a complete inert service fixture. */
static void fixture(void)
{
    TestRuntimeReset();memset(&health,0,sizeof(health));memset(&hUsbDeviceFS,0,sizeof(hUsbDeviceFS));
    started=clock_ready=vbus_lost=false;rx_head=rx_tail=0U;
    inits=attaches=detaches=pullup_drops=tx_launches=0U;lose_during_init=busy=false;
    assert(AtlasUsb_Start()==ATLAS_OK);
}
/** @brief Check task context, bounded ring and stale-session tags without traffic. */
static void stream_cases(void)
{
    fixture();uint8_t bytes[64];memset(bytes,0x42,sizeof(bytes));
    assert(AtlasUsb_Write(bytes,1U)==ATLAS_ERROR_STATE);
    test_scheduler=taskSCHEDULER_RUNNING;
    assert(AtlasUsb_Write(bytes,1U)==ATLAS_ERROR_NOT_READY);
    GPIOA->IDR=USB_VBUS_Pin;AtlasUsb_OnClassState(true);
    for(unsigned i=0;i<16U;++i)AtlasUsb_OnReceive(bytes,sizeof(bytes));
    AtlasUsb_OnReceive(bytes,64U);
    assert(health.rx_bytes==1024U && health.rx_dropped_bytes==64U);
    uint8_t output[128];assert(AtlasUsb_Read(output,sizeof(output))==64U);
    assert(memcmp(output,bytes,64U)==0);
    AtlasUsb_OnClassState(false);
    assert(AtlasUsb_Read(output,sizeof(output))==0U && health.rx_dropped_bytes==1024U);
    AtlasUsb_OnClassState(true);
    for(unsigned i=0;i<4U;++i)assert(AtlasUsb_Write(bytes,4U)==ATLAS_OK);
    assert(AtlasUsb_Write(bytes,4U)==ATLAS_ERROR_BUSY);
    memset(bytes,0,sizeof(bytes));UsbPacket packet;
    assert(xQueuePeek(tx_queue,&packet,0U)==pdTRUE && packet.data[0]==0x42U);
    uint32_t previous=packet.session;AtlasUsb_OnClassState(false);AtlasUsb_OnClassState(true);
    assert(previous!=health.session);
    test_ipsr=1U;assert(AtlasUsb_Read(output,64U)==0U && AtlasUsb_Write(bytes,1U)==ATLAS_ERROR_STATE);
    test_ipsr=0U;test_scheduler=taskSCHEDULER_SUSPENDED;
    assert(AtlasUsb_Write(bytes,1U)==ATLAS_ERROR_STATE);
}
/** @brief Inject a queued packet immediately after attach. @param iteration Loop count. */
static void unplug_script(unsigned iteration)
{
    uint8_t data[3]={1U,2U,3U};
    if(iteration==5U){assert(attaches==1U);assert(AtlasUsb_Write(data,3U)==ATLAS_OK);}
    if(iteration==6U)
    {
        assert(tx_launches==1U && busy && retained_length==3U && retained_tx[2]==3U);
        assert(AtlasUsb_Write(data,3U)==ATLAS_OK);
        GPIOA->IDR=0U;AtlasUsb_OnVbusEdge();assert(pullup_drops==1U);
    }
}
/** @brief Queue one transfer and advance its outstanding wait past the timeout.
 * @param iteration Loop count. */
static void timeout_script(unsigned iteration)
{
    const uint8_t data[4]={4U,3U,2U,1U};
    if(iteration==5U)assert(AtlasUsb_Write(data,4U)==ATLAS_OK);
    if(iteration==6U){assert(tx_launches==1U);test_tick+=2000U;}
}
/** @brief Run owner lifecycle tests. @return Zero on passing checks. */
int main(void)
{
    stream_cases();
    fixture();TestRunTask(10U,NULL);assert(inits==0U && attaches==0U);
    fixture();GPIOA->IDR=USB_VBUS_Pin;lose_during_init=true;TestRunTask(8U,NULL);
    assert(inits==1U && attaches==0U && detaches==1U && !clock_ready);
    fixture();GPIOA->IDR=USB_VBUS_Pin;TestRunTask(9U,unplug_script);
    assert(detaches==1U && !health.configured && health.tx_dropped_bytes==6U && tx_launches==1U);
    fixture();GPIOA->IDR=USB_VBUS_Pin;TestRunTask(8U,timeout_script);
    assert(health.tx_timeouts==1U && health.tx_dropped_bytes==4U && detaches==1U && tx_launches==1U);
    assert(health.status==ATLAS_ERROR_TIMEOUT);
    fixture();GPIOA->IDR=USB_VBUS_Pin;TestRunTask(14U,timeout_script);
    assert(attaches==2U && tx_launches==1U && health.tx_timeouts==1U); /* Fresh enumeration, no replay. */
    puts("USB owner: VBUS debounce/loss during init, copied queues, overflow, unplug and timeout PASS");
    return 0;
}
