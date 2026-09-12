/**
 * @file atlas_rfd900x.h
 * @brief RFD900x/SiK transparent serial link and guarded local AT configuration firmware.
 *
 * Major functions:
 * - AtlasRfd900x_Init(): starts transparent UART transport without altering the radio.
 * - AtlasRfd900x_EnterCommandMode(): enforces the SiK guard interval around "+++".
 * - AtlasRfd900x_Command(): executes a bounded local AT command transaction.
 * - AtlasRfd900x_SetParameter(): explicitly changes an S-register with optional save.
 */

#ifndef ATLAS_RFD900X_H
#define ATLAS_RFD900X_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"
#include "atlas_uart_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_RFD900X_RESPONSE_CAPACITY (768U)

/** @brief Radio transport and command-mode diagnostics. */
typedef struct
{
    uint32_t payload_bytes_written;
    uint32_t payload_bytes_read;
    uint32_t command_entries;
    uint32_t commands_sent;
    uint32_t command_timeouts;
    uint32_t command_errors;
    uint32_t guard_rejections;
    uint32_t malformed_responses;
    uint32_t configuration_mismatches;
} AtlasRfd900xHealth;

/** @brief External RFD900x radio driver instance. */
typedef struct
{
    AtlasUartTransport *transport;
    bool initialized;
    bool command_mode;
    AtlasRfd900xHealth health;
} AtlasRfd900x;

/**
 * @brief Start the USART3 transport without sending or changing any modem setting.
 * @param radio Destination driver instance.
 * @param transport Destination UART transport object.
 * @param uart Initialized USART3 at the radio's currently configured serial baud.
 * @return ATLAS_OK or a typed transport failure.
 * @note RFD factory serial speed is commonly 57600; this board currently initializes 115200.
 */
AtlasStatus AtlasRfd900x_Init(AtlasRfd900x *radio,
                              AtlasUartTransport *transport,
                              UART_HandleTypeDef *uart);

/**
 * @brief Write transparent flight-link bytes.
 * @param radio Initialized radio in transparent mode.
 * @param data Payload bytes.
 * @param length Byte count.
 * @param timeout_ms Nonzero transmit timeout.
 * @return ATLAS_OK or a typed state/transport failure.
 */
AtlasStatus AtlasRfd900x_Write(AtlasRfd900x *radio,
                               const uint8_t *data,
                               size_t length,
                               uint32_t timeout_ms);

/**
 * @brief Read currently buffered transparent flight-link bytes.
 * @param radio Initialized radio in transparent mode.
 * @param destination Destination bytes.
 * @param capacity Maximum byte count.
 * @return Number of bytes copied.
 */
size_t AtlasRfd900x_Read(AtlasRfd900x *radio,
                         uint8_t *destination,
                         size_t capacity);

/**
 * @brief Enter local SiK command mode using the guarded "+++" sequence.
 * @param radio Initialized radio in transparent mode.
 * @return ATLAS_OK after the modem replies OK, BUSY if one second of prior silence is absent,
 *         or a typed timeout/transport failure.
 * @warning The caller must stop all application radio writes before calling this function.
 */
AtlasStatus AtlasRfd900x_EnterCommandMode(AtlasRfd900x *radio);

/**
 * @brief Exit local command mode and resume transparent operation.
 * @param radio Initialized radio in command mode.
 * @return ATLAS_OK after ATO succeeds or a typed state/transport failure.
 */
AtlasStatus AtlasRfd900x_ExitCommandMode(AtlasRfd900x *radio);

/**
 * @brief Execute one local SiK AT command and capture lines preceding terminal OK.
 * @param radio Initialized radio in command mode.
 * @param command NUL-terminated command beginning with AT and containing no CR/LF.
 * @param response Optional response destination.
 * @param response_capacity Destination capacity including NUL.
 * @param timeout_ms Nonzero response timeout.
 * @return ATLAS_OK, NACK on terminal ERROR, OVERFLOW on truncated response, or
 *         another typed timeout/argument/state failure.
 */
AtlasStatus AtlasRfd900x_Command(AtlasRfd900x *radio,
                                 const char *command,
                                 char *response,
                                 size_t response_capacity,
                                 uint32_t timeout_ms);

/**
 * @brief Query modem identity with ATI; never entered automatically during flight startup.
 * @param radio Initialized radio in command mode.
 * @param identity Destination identity text.
 * @param identity_capacity Destination capacity including NUL.
 * @return ATLAS_OK only after a terminal OK and substantive non-echo content.
 */
AtlasStatus AtlasRfd900x_ReadIdentity(AtlasRfd900x *radio,
                                      char *identity,
                                      size_t identity_capacity);

/**
 * @brief Read the complete local S-register listing using ATI5.
 * @param radio Initialized radio in command mode.
 * @param settings Destination text.
 * @param settings_capacity Destination capacity including NUL.
 * @return ATLAS_OK only after a terminal OK and substantive non-echo content.
 */
AtlasStatus AtlasRfd900x_ReadSettings(AtlasRfd900x *radio,
                                      char *settings,
                                      size_t settings_capacity);

/**
 * @brief Change and read back one local SiK S-register, then optionally save settings.
 * @param radio Initialized radio in command mode.
 * @param index S-register index 0..255.
 * @param value Unsigned register value.
 * @param persist true to issue AT&W after the acknowledged ATS command.
 * @return ATLAS_OK only after the write is acknowledged, ATSn? equals value, and
 *         an optional AT&W is acknowledged.
 * @warning RF frequency/power/duty settings must comply with local law and the paired modem.
 */
AtlasStatus AtlasRfd900x_SetParameter(AtlasRfd900x *radio,
                                      uint8_t index,
                                      uint32_t value,
                                      bool persist);

/**
 * @brief Explicitly change only the MCU-side USART3 baud rate.
 * @param radio Initialized radio instance.
 * @param baud_rate Baud already configured in the attached modem.
 * @return ATLAS_OK or a typed UART failure.
 * @warning This does not alter the radio's SERIAL_SPEED S-register.
 */
AtlasStatus AtlasRfd900x_ReconfigureHostBaud(AtlasRfd900x *radio,
                                             uint32_t baud_rate);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_RFD900X_H */
