/**
 * @file atlas_usb.c
 * @brief VBUS-aware CDC task, retained transmission and bounded receive stream.
 *
 * Major functions:
 * - usb_task(): owns library init/deinit, attach debounce and TX timeout recovery.
 * - AtlasUsb_Read()/Write(): bounded task APIs with copied buffers/session tags.
 * - AtlasUsb_OnReceive()/OnTransmit(): ISR-side data movement and diagnostics.
 * - AtlasUsb_OnVbusEdge(): removes the pull-up immediately; teardown is task-owned.
 *
 * IRQ-masked regions contain at most one 64-byte copy or a short register/library
 * operation. Slow USB initialization and FIFO teardown never mask the HAL tick.
 * USB OTG internal DMA remains OFF: DTCM buffers are CPU/FIFO accessed.
 */
#include "atlas_usb.h"
#include "main.h"
#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_cdc_if.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>

#define USB_STACK_WORDS (1024U)
#define USB_TX_QUEUE_LENGTH (4U)
#define USB_TX_TIMEOUT_MS (2000U)
typedef struct { uint32_t session; uint16_t length; uint8_t data[64]; } UsbPacket;
static QueueHandle_t tx_queue;
static StaticQueue_t queue_control;
static uint8_t queue_memory[USB_TX_QUEUE_LENGTH * sizeof(UsbPacket)];
static StaticTask_t task_control;
static StackType_t task_stack[USB_STACK_WORDS];
static uint8_t rx_ring[ATLAS_USB_RX_CAPACITY];
static uint32_t rx_head, rx_tail;
static AtlasUsbHealth health;
static volatile bool clock_ready, vbus_lost;
static bool started;
extern USBD_HandleTypeDef hUsbDeviceFS;

/** @brief Enter a short task/ISR-safe exclusion region. @return Saved PRIMASK. */
static uint32_t usb_lock(void)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return mask;
}
/** @brief Restore IRQ masking. @param mask Previously saved PRIMASK. */
static void usb_unlock(uint32_t mask) { __DMB(); __set_PRIMASK(mask); }
/** @brief Read the reviewed PA9 external divider. @return VBUS indication. */
static bool usb_vbus(void)
{
    return HAL_GPIO_ReadPin(USB_VBUS_GPIO_Port, USB_VBUS_Pin) == GPIO_PIN_SET;
}
/** @brief Validate application call context. @return Running task context. */
static bool usb_context(void)
{
    return started && __get_IPSR() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

/** @brief Reset per-session stream state. @param configured Class state. */
void AtlasUsb_OnClassState(bool configured)
{
    const uint32_t mask = usb_lock();
    ++health.session;
    health.rx_dropped_bytes += rx_head - rx_tail;
    rx_head = rx_tail = 0U;
    health.configured = configured;
    health.dtr = health.rts = false;
    usb_unlock(mask);
}
/** @brief Retain an entire packet or count its loss. @param data Bytes. @param length Count. */
void AtlasUsb_OnReceive(const uint8_t *data, uint32_t length)
{
    const uint32_t mask = usb_lock();
    if (data == NULL || length > ATLAS_USB_PACKET_CAPACITY ||
        length > ATLAS_USB_RX_CAPACITY - (rx_head - rx_tail) || !health.configured)
        health.rx_dropped_bytes += length;
    else
    {
        for (uint32_t i = 0U; i < length; ++i)
            rx_ring[(rx_head++) % ATLAS_USB_RX_CAPACITY] = data[i];
        health.rx_bytes += length;
    }
    usb_unlock(mask);
}
/** @brief Record acknowledged endpoint transfer. @param length Completed bytes. */
void AtlasUsb_OnTransmit(uint32_t length)
{
    const uint32_t mask = usb_lock();
    health.tx_completed_bytes += length;
    usb_unlock(mask);
}
/** @brief Observe informational CDC control lines. @param value DTR/RTS flags. */
void AtlasUsb_OnControlLines(uint16_t value)
{
    const uint32_t mask = usb_lock();
    health.dtr = (value & 1U) != 0U;
    health.rts = (value & 2U) != 0U;
    usb_unlock(mask);
}
/** @brief Disconnect promptly on falling VBUS without invoking locking HAL APIs. */
void AtlasUsb_OnVbusEdge(void)
{
    if (!usb_vbus())
    {
        vbus_lost = true;
        /* The reviewed LL routine only ungates the PHY and sets DCTL.SDIS.
           It is bounded and takes no HAL handle lock; no RTOS API is used. */
        if (clock_ready) (void)USB_DevDisconnect(USB_OTG_FS);
    }
}

/** @brief Tear down with the USB ISR excluded, but keep system ticks running. */
static void usb_detach(void)
{
    uint32_t mask = usb_lock();
    clock_ready = false;
    health.configured = false;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    usb_unlock(mask);
    (void)USBD_DeInit(&hUsbDeviceFS);
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    AtlasUsb_OnClassState(false);
}

/** @brief Own USB attach and bounded transmission. @param argument Unused. */
static void usb_task(void *argument)
{
    bool initialized = false, have_packet = false, transmitting = false;
    uint32_t stable_since = HAL_GetTick(), tx_started = 0U;
    UsbPacket packet;
    (void)argument;
    for (;;)
    {
        const uint32_t now = HAL_GetTick();
        const bool present = usb_vbus();
        uint32_t mask = usb_lock();
        health.vbus = present;
        const bool lost = vbus_lost;
        vbus_lost = false;
        usb_unlock(mask);
        if (!present || lost)
        {
            if (initialized) { usb_detach(); initialized = false; }
            stable_since = now;
        }
        if (present && !initialized && (uint32_t)(now - stable_since) >= 20U)
        {
            const uint8_t status = AtlasUsbDevice_Init();
            initialized = status == USBD_OK;
            mask = usb_lock();
            clock_ready = initialized;
            /* Check again after potentially slow initialization; never attach on stale VBUS. */
            if (initialized && usb_vbus() && !vbus_lost)
                health.status = USBD_Start(&hUsbDeviceFS) == USBD_OK ? ATLAS_OK : ATLAS_ERROR_IO;
            else health.status = ATLAS_ERROR_NOT_READY;
            const bool attach_ok = health.status == ATLAS_OK;
            usb_unlock(mask);
            if (!attach_ok)
            {
                usb_detach();
                initialized = false;
                stable_since = HAL_GetTick();
            }
        }
        if (!have_packet && xQueueReceive(tx_queue, &packet, 0U) == pdTRUE)
            have_packet = true;
        mask = usb_lock();
        if (have_packet && (!health.configured || packet.session != health.session || !present))
        {
            health.tx_dropped_bytes += packet.length;
            have_packet = transmitting = false;
        }
        if (have_packet && !transmitting)
        {
            const uint8_t result = CDC_Transmit_FS(packet.data, packet.length);
            if (result == USBD_OK) { transmitting = true; tx_started = HAL_GetTick(); }
            else if (result != USBD_BUSY)
            {
                health.tx_dropped_bytes += packet.length;
                have_packet = false;
            }
        }
        if (transmitting && !CDC_TransmitBusy_FS()) have_packet = transmitting = false;
        const bool timeout = transmitting && (uint32_t)(HAL_GetTick() - tx_started) >= USB_TX_TIMEOUT_MS;
        if (timeout)
        {
            ++health.tx_timeouts;
            health.tx_dropped_bytes += packet.length;
            have_packet = transmitting = false;
            health.status = ATLAS_ERROR_TIMEOUT;
        }
        usb_unlock(mask);
        if (timeout) { usb_detach(); initialized = false; stable_since = HAL_GetTick(); }
        const uint32_t stack_free = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        mask = usb_lock();
        health.stack_free_words = stack_free;
        usb_unlock(mask);
        vTaskDelay(pdMS_TO_TICKS(5U) != 0U ? pdMS_TO_TICKS(5U) : 1U);
    }
}

/** @brief Create USB objects before scheduling. @return Atlas status. */
AtlasStatus AtlasUsb_Start(void)
{
    if (started || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) return ATLAS_ERROR_STATE;
    tx_queue = xQueueCreateStatic(USB_TX_QUEUE_LENGTH, sizeof(UsbPacket), queue_memory, &queue_control);
    if (tx_queue == NULL || xTaskCreateStatic(usb_task, "AtlasUSB", USB_STACK_WORDS, NULL,
        2U, task_stack, &task_control) == NULL) return ATLAS_ERROR_STATE;
    health.status = ATLAS_ERROR_NOT_READY;
    started = true;
    return ATLAS_OK;
}
/** @brief Copy at most one packet-sized chunk. @param data Destination.
 * @param capacity Space. @return Received byte count. */
size_t AtlasUsb_Read(uint8_t *data, size_t capacity)
{
    if (data == NULL || !usb_context()) return 0U;
    if (capacity > ATLAS_USB_PACKET_CAPACITY) capacity = ATLAS_USB_PACKET_CAPACITY;
    size_t count = 0U;
    const uint32_t mask = usb_lock();
    while (count < capacity && rx_tail != rx_head)
        data[count++] = rx_ring[(rx_tail++) % ATLAS_USB_RX_CAPACITY];
    usb_unlock(mask);
    return count;
}
/** @brief Queue copied data tagged with the current USB session. @param data Source.
 * @param length 1-64 bytes. @return Acceptance status. */
AtlasStatus AtlasUsb_Write(const uint8_t *data, size_t length)
{
    UsbPacket packet;
    if (data == NULL) return ATLAS_ERROR_NULL;
    if (length == 0U || length > ATLAS_USB_PACKET_CAPACITY) return ATLAS_ERROR_ARGUMENT;
    if (!usb_context()) return ATLAS_ERROR_STATE;
    const uint32_t mask = usb_lock();
    const bool ready = health.configured && usb_vbus() && !vbus_lost;
    packet.session = health.session;
    usb_unlock(mask);
    if (!ready) return ATLAS_ERROR_NOT_READY;
    packet.length = (uint16_t)length;
    memcpy(packet.data, data, length);
    return xQueueSend(tx_queue, &packet, 0U) == pdTRUE ? ATLAS_OK : ATLAS_ERROR_BUSY;
}
/** @brief Copy coherent diagnostics. @param output Destination. @return Readiness. */
bool AtlasUsb_GetHealth(AtlasUsbHealth *output)
{
    if (output == NULL || !usb_context()) return false;
    const uint32_t mask = usb_lock();
    *output = health;
    usb_unlock(mask);
    return true;
}
