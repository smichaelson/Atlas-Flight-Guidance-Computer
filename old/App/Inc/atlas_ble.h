/**
 * @file atlas_ble.h
 * @brief u-blox NINA-B112 u-connectXpress AT-command and data transport firmware.
 *
 * Major functions:
 * - AtlasBle_Init(): boots in normal mode and proves command-channel communication.
 * - AtlasBle_Command(): executes one bounded AT transaction and classifies its result.
 * - AtlasBle_ConfigureSps(): explicitly prepares peripheral/SPS startup settings.
 * - AtlasBle_EnterCommandMode()/EnterDataMode(): perform documented mode transitions.
 * - AtlasBle_WriteData()/ReadData(): move bytes after the module enters data mode.
 */

#ifndef ATLAS_BLE_H
#define ATLAS_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"
#include "atlas_uart_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_BLE_IDENTITY_CAPACITY (96U)
#define ATLAS_BLE_LINE_CAPACITY     (160U)

/** @brief BLE transport and AT transaction diagnostics. */
typedef struct
{
    uint32_t resets;
    uint32_t commands_sent;
    uint32_t command_ok;
    uint32_t command_errors;
    uint32_t command_timeouts;
    uint32_t response_overflows;
    uint32_t configuration_mismatches;
    uint32_t mode_transitions;
    uint32_t mode_transition_failures;
    uint32_t data_bytes_written;
    uint32_t data_bytes_read;
} AtlasBleHealth;

/** @brief NINA-B112 driver instance. */
typedef struct
{
    AtlasUartTransport *transport;
    char model[ATLAS_BLE_IDENTITY_CAPACITY];
    char firmware[ATLAS_BLE_IDENTITY_CAPACITY];
    bool initialized;
    bool command_mode;
    AtlasBleHealth health;
} AtlasBle;

/**
 * @brief Boot NINA-B112 in normal mode and prove two-way AT communication.
 * @param ble Destination driver instance.
 * @param transport Destination UART transport object.
 * @param uart Initialized USART6 at 115200 8N1 with RTS/CTS enabled.
 * @return ATLAS_OK or a typed transport, command, timeout, or identity failure.
 * @warning SWITCH1/SWITCH2 are forced high; factory restore/bootloader modes are never selected.
 */
AtlasStatus AtlasBle_Init(AtlasBle *ble,
                          AtlasUartTransport *transport,
                          UART_HandleTypeDef *uart);

/**
 * @brief Hardware-reset the module while preserving normal-mode switch levels.
 * @param ble Bound driver instance.
 * @return ATLAS_OK or ATLAS_ERROR_NULL.
 * @note The saved startup profile owns the resulting mode; call AtlasBle_Init()
 *       to identify an unknown profile or use the configured maintenance path.
 */
AtlasStatus AtlasBle_Reset(AtlasBle *ble);

/**
 * @brief Execute one AT command and collect nonterminal response lines.
 * @param ble Bound driver instance in command mode.
 * @param command NUL-terminated command beginning with "AT" and containing no CR/LF.
 * @param response Optional destination for response lines before OK/ERROR.
 * @param response_capacity Destination capacity including NUL; zero when response is NULL.
 * @param timeout_ms Nonzero transaction timeout.
 * @return ATLAS_OK on terminal OK, NACK on ERROR/CME ERROR, OVERFLOW if any
 *         response text would be truncated, or another typed failure.
 */
AtlasStatus AtlasBle_Command(AtlasBle *ble,
                             const char *command,
                             char *response,
                             size_t response_capacity,
                             uint32_t timeout_ms);

/**
 * @brief Configure BLE peripheral role, local name, and SPS startup mode.
 * @param ble Initialized driver instance.
 * @param device_name Printable local name, 1..29 characters, without quotes/commas.
 * @param persist true to issue AT&W, restart, prove wired command entry, read
 *        the saved profile back, and return to Data mode.
 * @return ATLAS_OK only when every command and queryable readback succeeds.
 * @warning persist writes module nonvolatile memory and deliberately restarts the module.
 */
AtlasStatus AtlasBle_ConfigureSps(AtlasBle *ble,
                                  const char *device_name,
                                  bool persist);

/**
 * @brief Enter command mode using the wired UART_DSR asserted-to-deasserted transition.
 * @param ble Initialized driver instance.
 * @return ATLAS_OK only after the transition's unsolicited OK and a subsequent
 *         AT probe both receive terminal OK.
 * @note The transition relies on the documented/default AT&D1 behavior established by
 *       AtlasBle_ConfigureSps(). It does not send an in-band escape sequence.
 */
AtlasStatus AtlasBle_EnterCommandMode(AtlasBle *ble);

/**
 * @brief Enter transparent data mode with ATO1.
 * @param ble Initialized driver instance in command mode.
 * @return ATLAS_OK only after ATO1 receives terminal OK.
 * @note The caller must wait for a BLE peer connection before expecting payload delivery.
 */
AtlasStatus AtlasBle_EnterDataMode(AtlasBle *ble);

/**
 * @brief Write transparent payload bytes while the module is in data mode.
 * @param ble Initialized driver instance.
 * @param data Payload bytes.
 * @param length Byte count.
 * @param timeout_ms Nonzero bounded transmit timeout.
 * @return ATLAS_OK or a typed state/transport failure.
 */
AtlasStatus AtlasBle_WriteData(AtlasBle *ble,
                               const uint8_t *data,
                               size_t length,
                               uint32_t timeout_ms);

/**
 * @brief Read currently buffered transparent payload bytes.
 * @param ble Initialized driver instance.
 * @param destination Destination buffer.
 * @param capacity Maximum byte count.
 * @return Number of bytes copied; zero for invalid arguments or no available bytes.
 */
size_t AtlasBle_ReadData(AtlasBle *ble,
                         uint8_t *destination,
                         size_t capacity);

/**
 * @brief Read the active-low module DTR/connection-control signal.
 * @param ble Driver instance.
 * @return true when the physical DTR input to the MCU is low.
 */
bool AtlasBle_IsDtrAsserted(const AtlasBle *ble);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_BLE_H */
