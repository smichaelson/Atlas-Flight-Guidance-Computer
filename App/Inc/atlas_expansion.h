/**
 * @file atlas_expansion.h
 * @brief RTOS-owned raw UART4, I2C2 and shared SPI3 expansion transactions.
 * Major functions: Start binds peripherals; Submit/Receive exchange copied work;
 * ReadUart consumes buffered UART bytes; Service runs only in the sensor-I/O owner.
 * Device-specific protocols, bus electrical compatibility and framing remain with
 * the attached-device driver. No addresses are scanned and no traffic is automatic.
 */
#ifndef ATLAS_EXPANSION_H
#define ATLAS_EXPANSION_H
#include "main.h"
#include "atlas_status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ATLAS_EXPANSION_PAYLOAD (32U)
#define ATLAS_EXPANSION_QUEUE_LENGTH (4U)
/** @brief Supported transactions; I2C addresses are UNshifted seven-bit values. */
typedef enum
{
    ATLAS_EXP_UART_WRITE = 0, ATLAS_EXP_UART_HOST_BAUD,
    ATLAS_EXP_I2C_WRITE, ATLAS_EXP_I2C_READ, ATLAS_EXP_I2C_REGISTER_WRITE,
    ATLAS_EXP_I2C_REGISTER_READ, ATLAS_EXP_SPI_EXCHANGE
} AtlasExpansionOperation;
/** @brief Value-owned request; raw UART is 8N1, SPI uses generated mode 3 / 3.125 MHz. */
typedef struct
{
    AtlasExpansionOperation operation;
    uint8_t length, address_7bit;
    bool register_16bit;
    uint16_t register_address;
    uint32_t baud_rate;
    uint8_t data[ATLAS_EXPANSION_PAYLOAD];
} AtlasExpansionRequest;
/** @brief Retained completion; length is zero on error, no automatic write retries. */
typedef struct
{
    uint32_t ticket;
    AtlasExpansionOperation operation;
    AtlasStatus status;
    uint8_t length, data[ATLAS_EXPANSION_PAYLOAD];
} AtlasExpansionResult;
/** @brief Read-only transport/queue diagnostics. */
typedef struct
{
    AtlasStatus uart_status, last_status;
    uint32_t completed, uart_dropped, uart_transport_dropped, uart_errors, uart_restarts;
} AtlasExpansionHealth;
/** @brief Bind once before scheduling; does not identify or configure external devices.
 * @param uart Generated UART4. @param i2c Generated I2C2. @param spi Shared SPI3.
 * @return Startup status. CS_SPI_EXT remains deasserted. */
AtlasStatus AtlasExpansion_Start(UART_HandleTypeDef *uart, I2C_HandleTypeDef *i2c, SPI_HandleTypeDef *spi);
/** @brief Submit one copied, nonblocking transaction. @param request Request.
 * @param ticket Optional identifier. @return Acceptance status, not peripheral acknowledgment. */
AtlasStatus AtlasExpansion_Submit(const AtlasExpansionRequest *request, uint32_t *ticket);
/** @brief Consume one result without blocking. @param result Destination.
 * @return true when available. A designated consumer must drain results. */
bool AtlasExpansion_Receive(AtlasExpansionResult *result);
/** @brief Read at most 64 UART bytes, without blocking. @param data Destination.
 * @param capacity Buffer capacity. @return Copied byte count; task-only, one reader. */
size_t AtlasExpansion_ReadUart(uint8_t *data, size_t capacity);
/** @brief Copy diagnostics. @param health Destination. @return Success in task context. */
bool AtlasExpansion_GetHealth(AtlasExpansionHealth *health);
/** @brief Internal sole-owner poll; never call from an application task or ISR.
 * @param allow_transaction Permit at most one 5 ms HAL transaction this cycle.
 * @return true when a queued request was consumed. UART reception is always drained. */
bool AtlasExpansion_Service(bool allow_transaction);
#endif
