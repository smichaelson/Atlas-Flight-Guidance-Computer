/**
 * @file atlas_uart_transport.c
 * @brief Interrupt-driven UART buffering and bounded foreground transmission.
 *
 * Major functions:
 * - AtlasUartTransport_Init()/Start(): bind, clear stale RX state, and arm interrupts.
 * - AtlasUartTransport_Read()/Write(): provide loss-accounted stream I/O.
 * - AtlasUartTransport_Service(): restart receive after framing/overrun errors.
 * - HAL_UARTEx_RxEventCallback()/HAL_UART_ErrorCallback(): minimal ISR dispatch.
 */

#include "atlas_uart_transport.h"

#include <string.h>

#if ((ATLAS_UART_RX_RING_CAPACITY & (ATLAS_UART_RX_RING_CAPACITY - 1U)) != 0U)
#error "ATLAS_UART_RX_RING_CAPACITY must be a power of two"
#endif

static AtlasUartTransport *atlas_uart_registry[ATLAS_UART_REGISTRY_CAPACITY];

/**
 * @brief Retain the most recent HAL result and UART error bits for diagnostics.
 * @param transport Transport whose health record is updated.
 * @param hal_status HAL call result.
 */
static void atlas_uart_record_hal(AtlasUartTransport *transport,
                                  HAL_StatusTypeDef hal_status)
{
    transport->health.last_hal_status = (uint32_t)hal_status;
    transport->health.last_hal_error = (uint32_t)transport->uart->ErrorCode;
}

/**
 * @brief Translate a HAL result to a common Atlas status.
 * @param hal_status STM32 HAL result.
 * @return Corresponding Atlas status.
 */
static AtlasStatus atlas_uart_from_hal(HAL_StatusTypeDef hal_status)
{
    if (hal_status == HAL_OK)
    {
        return ATLAS_OK;
    }
    if (hal_status == HAL_BUSY)
    {
        return ATLAS_ERROR_BUSY;
    }
    if (hal_status == HAL_TIMEOUT)
    {
        return ATLAS_ERROR_TIMEOUT;
    }
    return ATLAS_ERROR_IO;
}

/**
 * @brief Find the registered transport for a HAL UART handle.
 * @param uart HAL UART handle to locate.
 * @return Registered transport or NULL.
 */
static AtlasUartTransport *atlas_uart_find(UART_HandleTypeDef *uart)
{
    size_t index;

    for (index = 0U; index < ATLAS_UART_REGISTRY_CAPACITY; ++index)
    {
        if ((atlas_uart_registry[index] != NULL) &&
            (atlas_uart_registry[index]->uart == uart))
        {
            return atlas_uart_registry[index];
        }
    }
    return NULL;
}

/**
 * @brief Arm a new receive-to-idle operation using the transport scratch buffer.
 * @param transport Initialized transport.
 * @return ATLAS_OK or translated HAL failure.
 */
static AtlasStatus atlas_uart_arm_receive(AtlasUartTransport *transport)
{
    HAL_StatusTypeDef hal_status;

    hal_status = HAL_UARTEx_ReceiveToIdle_IT(transport->uart,
                                             transport->rx_chunk,
                                             ATLAS_UART_RX_CHUNK_CAPACITY);
    atlas_uart_record_hal(transport, hal_status);
    if (hal_status != HAL_OK)
    {
        transport->running = false;
        transport->restart_requested = true;
        return atlas_uart_from_hal(hal_status);
    }

    transport->running = true;
    transport->restart_requested = false;
    return ATLAS_OK;
}

/**
 * @brief Abort any old receive transaction and clear stale FIFO/error state.
 * @param transport Initialized transport with a valid UART handle.
 * @return ATLAS_OK or a translated HAL abort failure.
 * @note NEO-M9N and other talkers may fill USART RX before a staged probe. The
 *       STM32 HAL can reject ReceiveToIdle when that pre-existing overrun is
 *       serviced during arming, so every non-running start begins with the HAL's
 *       documented error-clear and RX-flush path.
 */
static AtlasStatus atlas_uart_preflight_receive(AtlasUartTransport *transport)
{
    const HAL_StatusTypeDef hal_status = HAL_UART_AbortReceive(transport->uart);
    atlas_uart_record_hal(transport, hal_status);
    if (hal_status != HAL_OK)
    {
        return atlas_uart_from_hal(hal_status);
    }

    ++transport->health.receive_preflights;
    transport->rx_tail = transport->rx_head;
    __DMB();
    return ATLAS_OK;
}

/**
 * @brief Publish bytes from the ISR scratch buffer into the SPSC receive ring.
 * @param transport Destination transport.
 * @param size Number of valid bytes in rx_chunk.
 */
static void atlas_uart_publish_isr(AtlasUartTransport *transport, uint16_t size)
{
    uint16_t index;
    uint16_t head = transport->rx_head;

    if (size > ATLAS_UART_RX_CHUNK_CAPACITY)
    {
        size = ATLAS_UART_RX_CHUNK_CAPACITY;
    }

    for (index = 0U; index < size; ++index)
    {
        const uint16_t next = (uint16_t)((head + 1U) &
                                         (ATLAS_UART_RX_RING_CAPACITY - 1U));
        if (next == transport->rx_tail)
        {
            ++transport->health.dropped_bytes;
            continue;
        }

        transport->rx_ring[head] = transport->rx_chunk[index];
        /* Publish data before the producer index in this lock-free SPSC ring. */
        __DMB();
        head = next;
        transport->rx_head = head;
        ++transport->health.bytes_received;
    }
    transport->last_rx_ms = HAL_GetTick();
}

/**
 * @brief Bind a UART transport and add it to the HAL callback registry.
 * @param transport Destination transport object.
 * @param uart Initialized STM32 HAL UART handle.
 * @return ATLAS_OK, ATLAS_ERROR_NULL, or ATLAS_ERROR_OVERFLOW.
 */
AtlasStatus AtlasUartTransport_Init(AtlasUartTransport *transport,
                                    UART_HandleTypeDef *uart)
{
    size_t index;
    size_t free_index = ATLAS_UART_REGISTRY_CAPACITY;

    if ((transport == NULL) || (uart == NULL))
    {
        return ATLAS_ERROR_NULL;
    }

    memset(transport, 0, sizeof(*transport));
    transport->uart = uart;

    for (index = 0U; index < ATLAS_UART_REGISTRY_CAPACITY; ++index)
    {
        if (atlas_uart_registry[index] == transport)
        {
            return ATLAS_OK;
        }
        if ((atlas_uart_registry[index] != NULL) &&
            (atlas_uart_registry[index]->uart == uart))
        {
            return ATLAS_ERROR_STATE;
        }
        if ((atlas_uart_registry[index] == NULL) &&
            (free_index == ATLAS_UART_REGISTRY_CAPACITY))
        {
            free_index = index;
        }
    }

    if (free_index == ATLAS_UART_REGISTRY_CAPACITY)
    {
        return ATLAS_ERROR_OVERFLOW;
    }
    atlas_uart_registry[free_index] = transport;
    return ATLAS_OK;
}

/**
 * @brief Start receive-to-idle interrupt operation.
 * @param transport Initialized transport.
 * @return ATLAS_OK or a translated HAL failure.
 */
AtlasStatus AtlasUartTransport_Start(AtlasUartTransport *transport)
{
    AtlasStatus status;

    if (transport == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (transport->uart == NULL)
    {
        return ATLAS_ERROR_STATE;
    }
    if (transport->running)
    {
        return ATLAS_OK;
    }

    status = atlas_uart_preflight_receive(transport);
    if (status != ATLAS_OK)
    {
        return status;
    }
    status = atlas_uart_arm_receive(transport);
    if (status == ATLAS_OK)
    {
        return ATLAS_OK;
    }

    /* A continuously transmitting peer can race the short abort-to-arm window.
     * Retry the complete clear/flush/arm sequence once, never indefinitely. */
    ++transport->health.start_retries;
    status = atlas_uart_preflight_receive(transport);
    return status == ATLAS_OK ? atlas_uart_arm_receive(transport) : status;
}

/**
 * @brief Stop reception without deinitializing the UART peripheral.
 * @param transport Initialized transport.
 * @return ATLAS_OK or a translated HAL failure.
 */
AtlasStatus AtlasUartTransport_Stop(AtlasUartTransport *transport)
{
    HAL_StatusTypeDef hal_status;

    if (transport == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (transport->uart == NULL)
    {
        return ATLAS_ERROR_STATE;
    }

    transport->running = false;
    transport->restart_requested = false;
    hal_status = HAL_UART_AbortReceive(transport->uart);
    atlas_uart_record_hal(transport, hal_status);
    return atlas_uart_from_hal(hal_status);
}

/**
 * @brief Recover a receive operation that was stopped by a UART error.
 * @param transport Initialized transport.
 * @return ATLAS_OK when running, or a translated HAL failure.
 */
AtlasStatus AtlasUartTransport_Service(AtlasUartTransport *transport)
{
    AtlasStatus status;

    if (transport == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!transport->restart_requested)
    {
        return transport->running ? ATLAS_OK : ATLAS_ERROR_NOT_READY;
    }

    status = atlas_uart_preflight_receive(transport);
    if (status != ATLAS_OK)
    {
        return status;
    }
    ++transport->health.receive_restarts;
    return atlas_uart_arm_receive(transport);
}

/**
 * @brief Return the number of bytes currently buffered for foreground parsing.
 * @param transport Initialized transport.
 * @return Byte count in the receive ring; zero for NULL.
 */
size_t AtlasUartTransport_Available(const AtlasUartTransport *transport)
{
    uint16_t head;
    uint16_t tail;

    if (transport == NULL)
    {
        return 0U;
    }
    head = transport->rx_head;
    tail = transport->rx_tail;
    return (size_t)((head - tail) & (ATLAS_UART_RX_RING_CAPACITY - 1U));
}

/**
 * @brief Read one byte from the receive ring.
 * @param transport Initialized transport.
 * @param value Destination byte.
 * @return true if a byte was read; otherwise false.
 */
bool AtlasUartTransport_ReadByte(AtlasUartTransport *transport, uint8_t *value)
{
    uint16_t tail;

    if ((transport == NULL) || (value == NULL))
    {
        return false;
    }

    tail = transport->rx_tail;
    if (tail == transport->rx_head)
    {
        return false;
    }

    *value = transport->rx_ring[tail];
    /* Complete the read before publishing the consumer index back to the ISR. */
    __DMB();
    transport->rx_tail = (uint16_t)((tail + 1U) &
                                    (ATLAS_UART_RX_RING_CAPACITY - 1U));
    return true;
}

/**
 * @brief Read up to capacity bytes from the receive ring.
 * @param transport Initialized transport.
 * @param destination Output buffer.
 * @param capacity Maximum bytes to copy.
 * @return Number of bytes copied.
 */
size_t AtlasUartTransport_Read(AtlasUartTransport *transport,
                               uint8_t *destination,
                               size_t capacity)
{
    size_t copied = 0U;

    if ((transport == NULL) || (destination == NULL))
    {
        return 0U;
    }

    while ((copied < capacity) &&
           AtlasUartTransport_ReadByte(transport, &destination[copied]))
    {
        ++copied;
    }
    return copied;
}

/**
 * @brief Discard all currently buffered receive bytes.
 * @param transport Initialized transport.
 */
void AtlasUartTransport_FlushRx(AtlasUartTransport *transport)
{
    if (transport != NULL)
    {
        transport->rx_tail = transport->rx_head;
        __DMB();
    }
}

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
                                     uint32_t timeout_ms)
{
    HAL_StatusTypeDef hal_status;

    if ((transport == NULL) || (data == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if ((transport->uart == NULL) || !transport->running)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if ((length == 0U) || (length > UINT16_MAX) || (timeout_ms == 0U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    hal_status = HAL_UART_Transmit(transport->uart,
                                   (uint8_t *)(uintptr_t)data,
                                   (uint16_t)length,
                                   timeout_ms);
    atlas_uart_record_hal(transport, hal_status);
    if (hal_status == HAL_OK)
    {
        transport->health.bytes_transmitted += (uint32_t)length;
        transport->last_tx_ms = HAL_GetTick();
    }
    return atlas_uart_from_hal(hal_status);
}

/**
 * @brief Explicitly reinitialize the host UART with a new baud rate.
 * @param transport Initialized transport.
 * @param baud_rate Supported nonzero baud rate.
 * @return ATLAS_OK or a translated HAL failure.
 */
AtlasStatus AtlasUartTransport_ReconfigureBaud(AtlasUartTransport *transport,
                                               uint32_t baud_rate)
{
    HAL_StatusTypeDef hal_status;

    if (transport == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if ((transport->uart == NULL) || (baud_rate == 0U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    (void)AtlasUartTransport_Stop(transport);
    hal_status = HAL_UART_DeInit(transport->uart);
    if (hal_status != HAL_OK)
    {
        return atlas_uart_from_hal(hal_status);
    }

    transport->uart->Init.BaudRate = baud_rate;
    hal_status = HAL_UART_Init(transport->uart);
    if (hal_status != HAL_OK)
    {
        return atlas_uart_from_hal(hal_status);
    }
    if ((HAL_UARTEx_SetTxFifoThreshold(transport->uart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) ||
        (HAL_UARTEx_SetRxFifoThreshold(transport->uart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) ||
        (HAL_UARTEx_EnableFifoMode(transport->uart) != HAL_OK))
    {
        return ATLAS_ERROR_IO;
    }

    AtlasUartTransport_FlushRx(transport);
    return AtlasUartTransport_Start(transport);
}

/**
 * @brief Return the most recent receive or transmit activity time.
 * @param transport Initialized transport.
 * @return HAL millisecond tick of the most recent activity, or zero for NULL.
 */
uint32_t AtlasUartTransport_LastActivityMs(const AtlasUartTransport *transport)
{
    uint32_t rx;
    uint32_t tx;

    if (transport == NULL)
    {
        return 0U;
    }
    rx = transport->last_rx_ms;
    tx = transport->last_tx_ms;
    return ((int32_t)(rx - tx) >= 0) ? rx : tx;
}

/**
 * @brief Minimal HAL receive-event dispatcher; no protocol parsing occurs in ISR context.
 * @param uart HAL UART that generated the callback.
 * @param size Receive event size supplied by HAL.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
    AtlasUartTransport *transport = atlas_uart_find(uart);

    if (transport == NULL)
    {
        AtlasUartTransport_UnhandledRxEventHook(uart, size);
        return;
    }

    atlas_uart_publish_isr(transport, size);
    if (atlas_uart_arm_receive(transport) != ATLAS_OK)
    {
        transport->restart_requested = true;
    }
}

/**
 * @brief Minimal HAL UART error dispatcher; recovery is deferred to foreground service.
 * @param uart HAL UART that generated the callback.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    AtlasUartTransport *transport = atlas_uart_find(uart);

    if (transport == NULL)
    {
        AtlasUartTransport_UnhandledErrorHook(uart);
        return;
    }

    ++transport->health.uart_errors;
    transport->health.last_hal_error = (uint32_t)uart->ErrorCode;
    transport->running = false;
    transport->restart_requested = true;
}

/**
 * @brief Default no-op hook for unregistered UART receive events.
 * @param uart HAL UART that generated the callback.
 * @param size Receive event size supplied by HAL.
 */
__weak void AtlasUartTransport_UnhandledRxEventHook(UART_HandleTypeDef *uart, uint16_t size)
{
    (void)uart;
    (void)size;
}

/**
 * @brief Default no-op hook for errors on unregistered UARTs.
 * @param uart HAL UART that generated the callback.
 */
__weak void AtlasUartTransport_UnhandledErrorHook(UART_HandleTypeDef *uart)
{
    (void)uart;
}
