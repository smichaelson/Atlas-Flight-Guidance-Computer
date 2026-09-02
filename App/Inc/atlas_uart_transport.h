/**
 * @file atlas_uart_transport.h
 * @brief Interrupt-driven, allocation-free UART byte transport for Atlas modules.
 *
 * Major functions:
 * - AtlasUartTransport_Init()/Start(): register and arm receive-to-idle operation.
 * - AtlasUartTransport_Read()/Write(): move bounded byte streams in foreground code.
 * - AtlasUartTransport_Service(): recover reception after a UART hardware error.
 * - AtlasUartTransport_ReconfigureBaud(): explicitly change a module host baud rate.
 */

#ifndef ATLAS_UART_TRANSPORT_H
#define ATLAS_UART_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_UART_RX_RING_CAPACITY  (1024U)
#define ATLAS_UART_RX_CHUNK_CAPACITY (64U)
#define ATLAS_UART_REGISTRY_CAPACITY (4U)

/** @brief Counters retained for transport health and overflow diagnostics. */
typedef struct
{
    volatile uint32_t bytes_received;
    volatile uint32_t bytes_transmitted;
    volatile uint32_t dropped_bytes;
    volatile uint32_t uart_errors;
    volatile uint32_t receive_restarts;
} AtlasUartTransportHealth;

/**
 * @brief Single-producer/single-consumer UART receive transport.
 * @note The ISR is the only ring producer and foreground code is the only consumer.
 */
typedef struct
{
    UART_HandleTypeDef *uart;
    uint8_t rx_chunk[ATLAS_UART_RX_CHUNK_CAPACITY];
    uint8_t rx_ring[ATLAS_UART_RX_RING_CAPACITY];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    volatile bool running;
    volatile bool restart_requested;
    volatile uint32_t last_rx_ms;
    volatile uint32_t last_tx_ms;
    AtlasUartTransportHealth health;
} AtlasUartTransport;

/**
 * @brief Bind a UART transport and add it to the HAL callback registry.
 * @param transport Destination transport object.
 * @param uart Initialized STM32 HAL UART handle.
 * @return ATLAS_OK, ATLAS_ERROR_NULL, or ATLAS_ERROR_OVERFLOW if the registry is full.
 */
AtlasStatus AtlasUartTransport_Init(AtlasUartTransport *transport,
                                    UART_HandleTypeDef *uart);

/**
 * @brief Start receive-to-idle interrupt operation.
 * @param transport Initialized transport.
 * @return ATLAS_OK or a translated HAL failure.
 */
AtlasStatus AtlasUartTransport_Start(AtlasUartTransport *transport);

/**
 * @brief Stop reception without deinitializing the UART peripheral.
 * @param transport Initialized transport.
 * @return ATLAS_OK or a translated HAL failure.
 */
AtlasStatus AtlasUartTransport_Stop(AtlasUartTransport *transport);

/**
 * @brief Recover a receive operation that was stopped by a UART error.
 * @param transport Initialized transport.
 * @return ATLAS_OK when running, or a translated HAL failure.
 */
AtlasStatus AtlasUartTransport_Service(AtlasUartTransport *transport);

/**
 * @brief Return the number of bytes currently buffered for foreground parsing.
 * @param transport Initialized transport.
 * @return Byte count in the receive ring; zero for NULL.
 */
size_t AtlasUartTransport_Available(const AtlasUartTransport *transport);

/**
 * @brief Read up to capacity bytes from the receive ring.
 * @param transport Initialized transport.
 * @param destination Output buffer.
 * @param capacity Maximum bytes to copy.
 * @return Number of bytes copied.
 */
size_t AtlasUartTransport_Read(AtlasUartTransport *transport,
                               uint8_t *destination,
                               size_t capacity);

/**
 * @brief Read one byte from the receive ring.
 * @param transport Initialized transport.
 * @param value Destination byte.
 * @return true if a byte was read; false if no byte was available or an argument was NULL.
 */
bool AtlasUartTransport_ReadByte(AtlasUartTransport *transport, uint8_t *value);

/**
 * @brief Discard all currently buffered receive bytes.
 * @param transport Initialized transport.
 */
void AtlasUartTransport_FlushRx(AtlasUartTransport *transport);

/**
 * @brief Transmit a complete byte block with a bounded HAL timeout.
 * @param transport Initialized transport.
 * @param data Bytes to transmit.
 * @param length Byte count; range 1..65535.
 * @param timeout_ms Nonzero timeout in milliseconds.
 * @return ATLAS_OK or a translated HAL failure.
 */
AtlasStatus AtlasUartTransport_Write(AtlasUartTransport *transport,
                                     const uint8_t *data,
                                     size_t length,
                                     uint32_t timeout_ms);

/**
 * @brief Explicitly reinitialize the host UART with a new baud rate.
 * @param transport Initialized transport.
 * @param baud_rate Supported nonzero baud rate.
 * @return ATLAS_OK or a translated HAL failure.
 * @warning This changes only the MCU UART. The attached module must already use the same baud.
 */
AtlasStatus AtlasUartTransport_ReconfigureBaud(AtlasUartTransport *transport,
                                               uint32_t baud_rate);

/**
 * @brief Return the most recent receive or transmit activity time.
 * @param transport Initialized transport.
 * @return HAL millisecond tick of the most recent activity, or zero for NULL.
 */
uint32_t AtlasUartTransport_LastActivityMs(const AtlasUartTransport *transport);

/**
 * @brief Optional hook for receive events on a UART not registered with Atlas.
 * @param uart HAL UART that generated the callback.
 * @param size Receive event size supplied by HAL.
 */
void AtlasUartTransport_UnhandledRxEventHook(UART_HandleTypeDef *uart, uint16_t size);

/**
 * @brief Optional hook for errors on a UART not registered with Atlas.
 * @param uart HAL UART that generated the callback.
 */
void AtlasUartTransport_UnhandledErrorHook(UART_HandleTypeDef *uart);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_UART_TRANSPORT_H */
