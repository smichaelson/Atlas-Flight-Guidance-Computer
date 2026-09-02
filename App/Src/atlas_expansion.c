/**
 * @file atlas_expansion.c
 * @brief Bounded copied expansion work, executed by Atlas's shared-bus owner.
 * Major functions: Start, Submit, Service, Receive, ReadUart and GetHealth.
 * There is no extra task or competing SPI3 owner. A 5 ms HAL timeout bounds each
 * request's bus wait; scheduling/interrupt latency can extend wall-clock response.
 */
#include "atlas_expansion.h"
#include "atlas_spi_device.h"
#include "atlas_uart_transport.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>
#define EXP_TIMEOUT_MS (5U)
#define EXP_RX_CAPACITY (1024U)
typedef struct { AtlasExpansionRequest request; uint32_t ticket; } ExpansionQueued;
static AtlasUartTransport uart_transport;
static AtlasSpiDevice spi_device;
static I2C_HandleTypeDef *i2c_port;
static QueueHandle_t requests, results;
static StaticQueue_t requests_control, results_control;
static uint8_t request_memory[ATLAS_EXPANSION_QUEUE_LENGTH * sizeof(ExpansionQueued)];
static uint8_t result_memory[ATLAS_EXPANSION_QUEUE_LENGTH * sizeof(AtlasExpansionResult)];
static uint8_t rx[EXP_RX_CAPACITY];
static uint32_t head, tail, next_ticket;
static bool started;
static AtlasExpansionHealth health;

/** @brief Require active task context. @return API context validity. */
static bool expansion_context(void)
{ return started && __get_IPSR() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING; }
/** @brief Translate a HAL transaction result. @param status HAL status. @return Typed result. */
static AtlasStatus expansion_status(HAL_StatusTypeDef status)
{
    if (status == HAL_OK) return ATLAS_OK;
    if (status == HAL_TIMEOUT) return ATLAS_ERROR_TIMEOUT;
    if (status == HAL_BUSY) return ATLAS_ERROR_BUSY;
    return ATLAS_ERROR_IO;
}
/** @brief Check sizes and addressing before issuing any traffic. @param r Request.
 * @return true for supported, bounded operation arguments. */
static bool expansion_valid(const AtlasExpansionRequest *r)
{
    if ((uint32_t)r->operation > ATLAS_EXP_SPI_EXCHANGE) return false;
    if (r->operation == ATLAS_EXP_UART_HOST_BAUD) return r->baud_rate >= 9600U && r->baud_rate <= 1000000U;
    if (r->length == 0U || r->length > ATLAS_EXPANSION_PAYLOAD) return false;
    if (r->operation >= ATLAS_EXP_I2C_WRITE && r->operation <= ATLAS_EXP_I2C_REGISTER_READ)
        return r->address_7bit >= 8U && r->address_7bit <= 0x77U &&
               (r->register_16bit || r->register_address <= 0xFFU);
    return true;
}
/** @brief Bind hardware and static queues; outputs remain inactive. @param uart UART4.
 * @param i2c I2C2. @param spi SPI3. @return Startup status. */
AtlasStatus AtlasExpansion_Start(UART_HandleTypeDef *uart, I2C_HandleTypeDef *i2c, SPI_HandleTypeDef *spi)
{
    if (uart == NULL || i2c == NULL || spi == NULL) return ATLAS_ERROR_NULL;
    if (started || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) return ATLAS_ERROR_STATE;
    if (uart->Instance != UART4 || i2c->Instance != I2C2 || spi->Instance != SPI3) return ATLAS_ERROR_ARGUMENT;
    AtlasStatus status = AtlasUartTransport_Init(&uart_transport, uart);
    if (status == ATLAS_OK) status = AtlasUartTransport_Start(&uart_transport);
    if (status == ATLAS_OK) status = AtlasSpiDevice_Init(&spi_device, spi, CS_SPI_EXT_GPIO_Port, CS_SPI_EXT_Pin, EXP_TIMEOUT_MS);
    if (status != ATLAS_OK) return status;
    i2c_port = i2c;
    requests = xQueueCreateStatic(ATLAS_EXPANSION_QUEUE_LENGTH, sizeof(ExpansionQueued), request_memory, &requests_control);
    results = xQueueCreateStatic(ATLAS_EXPANSION_QUEUE_LENGTH, sizeof(AtlasExpansionResult), result_memory, &results_control);
    if (requests == NULL || results == NULL) return ATLAS_ERROR_STATE;
    started = true;
    return ATLAS_OK;
}
/** @brief Queue a copy, preserving caller-buffer lifetime. @param request Request.
 * @param ticket Optional identifier. @return Accepted or typed rejection. */
AtlasStatus AtlasExpansion_Submit(const AtlasExpansionRequest *request, uint32_t *ticket)
{
    if (request == NULL) return ATLAS_ERROR_NULL;
    if (!expansion_context()) return ATLAS_ERROR_STATE;
    if (!expansion_valid(request)) return ATLAS_ERROR_ARGUMENT;
    ExpansionQueued queued = {0};
    queued.request = *request;
    taskENTER_CRITICAL();
    queued.ticket = ++next_ticket;
    taskEXIT_CRITICAL();
    if (xQueueSend(requests, &queued, 0U) != pdTRUE) return ATLAS_ERROR_BUSY;
    if (ticket != NULL) *ticket = queued.ticket;
    return ATLAS_OK;
}
/** @brief Consume one retained completion. @param result Destination. @return Availability. */
bool AtlasExpansion_Receive(AtlasExpansionResult *result)
{ return result != NULL && expansion_context() && xQueueReceive(results, result, 0U) == pdTRUE; }
/** @brief Copy buffered UART data. @param data Destination. @param capacity Capacity.
 * @return Copied bytes; one reader is expected. */
size_t AtlasExpansion_ReadUart(uint8_t *data, size_t capacity)
{
    if (data == NULL || !expansion_context()) return 0U;
    if (capacity > ATLAS_UART_RX_CHUNK_CAPACITY) capacity = ATLAS_UART_RX_CHUNK_CAPACITY;
    size_t count = 0U;
    taskENTER_CRITICAL();
    while (count < capacity && tail != head) data[count++] = rx[(tail++) % EXP_RX_CAPACITY];
    taskEXIT_CRITICAL();
    return count;
}
/** @brief Obtain current diagnostics. @param destination Destination. @return Success. */
bool AtlasExpansion_GetHealth(AtlasExpansionHealth *destination)
{
    if (destination == NULL || !expansion_context()) return false;
    taskENTER_CRITICAL();
    *destination = health;
    taskEXIT_CRITICAL();
    return true;
}
/** @brief Execute a validated request on owned handles. @param r Request.
 * @param out Result payload. @return Operation status. */
static AtlasStatus expansion_execute(const AtlasExpansionRequest *r, AtlasExpansionResult *out)
{
    const uint16_t address = (uint16_t)r->address_7bit << 1;
    const uint16_t reg_size = r->register_16bit ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
    HAL_StatusTypeDef status;
    switch (r->operation)
    {
        case ATLAS_EXP_UART_WRITE:
            /* Preserve the short shared-owner budget after explicit low-baud
             * changes. A caller can split a longer stream into smaller requests. */
            if ((uint32_t)r->length * 10000U > uart_transport.uart->Init.BaudRate * (EXP_TIMEOUT_MS - 1U))
                return ATLAS_ERROR_ARGUMENT;
            return AtlasUartTransport_Write(&uart_transport, r->data, r->length, EXP_TIMEOUT_MS);
        case ATLAS_EXP_UART_HOST_BAUD:
            /* A baud change intentionally discards old-format bytes, with diagnostics. */
            taskENTER_CRITICAL();
            health.uart_dropped += head - tail;
            head = tail = 0U;
            taskEXIT_CRITICAL();
            return AtlasUartTransport_ReconfigureBaud(&uart_transport, r->baud_rate);
        case ATLAS_EXP_SPI_EXCHANGE:
            if (HAL_GPIO_ReadPin(CS_LSM6DSV16B_GPIO_Port, CS_LSM6DSV16B_Pin) != GPIO_PIN_SET) return ATLAS_ERROR_BUSY;
            out->length = r->length;
            return AtlasSpiDevice_Transfer(&spi_device, r->data, out->data, r->length);
        case ATLAS_EXP_I2C_WRITE:
            status = HAL_I2C_Master_Transmit(i2c_port, address, (uint8_t *)(uintptr_t)r->data, r->length, EXP_TIMEOUT_MS);
            break;
        case ATLAS_EXP_I2C_READ:
            out->length = r->length;
            status = HAL_I2C_Master_Receive(i2c_port, address, out->data, r->length, EXP_TIMEOUT_MS);
            break;
        case ATLAS_EXP_I2C_REGISTER_WRITE:
            status = HAL_I2C_Mem_Write(i2c_port, address, r->register_address, reg_size,
                                       (uint8_t *)(uintptr_t)r->data, r->length, EXP_TIMEOUT_MS);
            break;
        case ATLAS_EXP_I2C_REGISTER_READ:
            out->length = r->length;
            status = HAL_I2C_Mem_Read(i2c_port, address, r->register_address, reg_size, out->data, r->length, EXP_TIMEOUT_MS);
            break;
        default: return ATLAS_ERROR_ARGUMENT;
    }
    if (status == HAL_ERROR && (HAL_I2C_GetError(i2c_port) & HAL_I2C_ERROR_AF) != 0U) return ATLAS_ERROR_NACK;
    return expansion_status(status);
}
/** @brief Sole-owner polling; transport upkeep never waits on result consumers.
 * @param allow_transaction Allow one optional request. @return A request was consumed. */
bool AtlasExpansion_Service(bool allow_transaction)
{
    if (!expansion_context()) return false;
    const AtlasStatus uart_status = AtlasUartTransport_Service(&uart_transport);
    uint8_t bytes[ATLAS_UART_RX_CHUNK_CAPACITY];
    const size_t received = AtlasUartTransport_Read(&uart_transport, bytes, sizeof(bytes));
    taskENTER_CRITICAL();
    for (size_t i = 0; i < received; ++i)
    {
        if (head - tail < EXP_RX_CAPACITY) rx[(head++) % EXP_RX_CAPACITY] = bytes[i];
        else ++health.uart_dropped;
    }
    health.uart_status = uart_status;
    health.uart_errors = uart_transport.health.uart_errors;
    health.uart_transport_dropped = uart_transport.health.dropped_bytes;
    health.uart_restarts = uart_transport.health.receive_restarts;
    taskEXIT_CRITICAL();
    if (!allow_transaction || uxQueueSpacesAvailable(results) == 0U) return false;
    ExpansionQueued queued;
    if (xQueueReceive(requests, &queued, 0U) != pdTRUE) return false;
    AtlasExpansionResult result = {0};
    result.ticket = queued.ticket;
    result.operation = queued.request.operation;
    result.status = expansion_execute(&queued.request, &result);
    if (result.status != ATLAS_OK) { result.length = 0U; memset(result.data, 0, sizeof(result.data)); }
    configASSERT(xQueueSend(results, &result, 0U) == pdTRUE);
    taskENTER_CRITICAL();
    health.last_status = result.status;
    ++health.completed;
    taskEXIT_CRITICAL();
    return true;
}
