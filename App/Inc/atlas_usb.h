/**
 * @file atlas_usb.h
 * @brief Bounded USB CDC transport with VBUS gating and disconnect-safe ownership.
 *
 * Major functions:
 * - AtlasUsb_Start(): creates the static USB lifecycle owner.
 * - AtlasUsb_Read()/Write(): copy bytes, never retain application memory.
 * - AtlasUsb_GetHealth(): reports session, overflows, line state and timeouts.
 * - AtlasUsb_On*(): nonblocking callbacks reserved for the USB/EXTI adapters.
 */
#ifndef ATLAS_USB_H
#define ATLAS_USB_H
#include "atlas_status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ATLAS_USB_PACKET_CAPACITY (64U)
#define ATLAS_USB_RX_CAPACITY (1024U)

/** @brief A CDC byte stream, not an authenticated command channel. */
typedef struct
{
    bool vbus, configured, dtr, rts;
    AtlasStatus status;
    uint32_t session;
    uint32_t rx_bytes, rx_dropped_bytes, tx_completed_bytes, tx_dropped_bytes;
    uint32_t tx_timeouts, stack_free_words;
} AtlasUsbHealth;

/** @brief Create the static owner before scheduling, without connecting the bus.
 * @return ATLAS status. */
AtlasStatus AtlasUsb_Start(void);
/** @brief Read up to 64 bytes without waiting from task context.
 * @param data Destination. @param capacity Available space. @return Bytes copied. */
size_t AtlasUsb_Read(uint8_t *data, size_t capacity);
/** @brief Queue a copied 1-64 byte packet without waiting.
 * @param data Source. @param length Bytes. @return Acceptance, not host receipt.
 * @note Queued data is discarded on reset/disconnect; no cross-session replay. */
AtlasStatus AtlasUsb_Write(const uint8_t *data, size_t length);
/** @brief Copy coherent transport health. @param health Destination. @return Readiness. */
bool AtlasUsb_GetHealth(AtlasUsbHealth *health);
/** @brief Handle class configuration/teardown; adapter-only. @param configured State. */
void AtlasUsb_OnClassState(bool configured);
/** @brief Copy one completed OUT packet; USB callback only.
 * @param data Source. @param length Bytes (at most 64). */
void AtlasUsb_OnReceive(const uint8_t *data, uint32_t length);
/** @brief Record completed IN transfer; USB callback only. @param length Bytes. */
void AtlasUsb_OnTransmit(uint32_t length);
/** @brief Record CDC control lines; they never arm outputs. @param value DTR/RTS bits. */
void AtlasUsb_OnControlLines(uint16_t value);
/** @brief Immediately drop the pull-up on VBUS loss; GPIO callback only. */
void AtlasUsb_OnVbusEdge(void);
#endif
