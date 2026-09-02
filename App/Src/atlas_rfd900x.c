/**
 * @file atlas_rfd900x.c
 * @brief Safe RFD900x transparent transport and explicitly guarded SiK AT access.
 *
 * Major functions:
 * - AtlasRfd900x_Init(): performs no radio traffic and no configuration writes.
 * - AtlasRfd900x_EnterCommandMode(): refuses entry if the serial link is not quiet.
 * - AtlasRfd900x_Command(): recognizes OK/ERROR with bounded buffering and timeouts.
 * - AtlasRfd900x_SetParameter(): reads back changes before optional persistence.
 */

#include "atlas_rfd900x.h"

#include "atlas_time.h"

#include <string.h>

#define RFD_GUARD_TIME_MS        (1100U)
#define RFD_ENTRY_TIMEOUT_MS     (1600U)
#define RFD_COMMAND_TIMEOUT_MS   (1000U)
#define RFD_LINE_CAPACITY        (160U)
#define RFD_COMMAND_CAPACITY     (40U)

/** @brief Encode unsigned decimal without locale, varargs or allocator code.
 * @param destination At least 11 bytes (ten digits plus NUL).
 * @param value Integer. @return Number of digits, excluding NUL. */
static size_t atlas_rfd_decimal(char *destination, uint32_t value)
{
    char reverse[10] = {0};
    size_t count = 0U, written = 0U;
    do { reverse[count++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    while (count != 0U) destination[written++] = reverse[--count];
    destination[written] = '\0';
    return written;
}
/** @brief Build the complete bounded S-register write or query line.
 * @param command Output text. @param capacity Capacity including NUL.
 * @param index S-register number. @param value Requested value.
 * @param query True for ATSn?, false for ATSn=value. @return Formatting status. */
static AtlasStatus atlas_rfd_parameter_line(char *command, size_t capacity,
    uint8_t index, uint32_t value, bool query)
{
    /* Longest line: ATS + 255 + '=' + 4294967295 + NUL = 18 bytes. */
    char line[18] = "ATS";
    size_t used = 3U + atlas_rfd_decimal(line + 3U, index);
    line[used++] = query ? '?' : '=';
    if (!query) used += atlas_rfd_decimal(line + used, value);
    line[used] = '\0';
    if (command == NULL) return ATLAS_ERROR_NULL;
    if (capacity <= used) return ATLAS_ERROR_OVERFLOW;
    memcpy(command, line, used + 1U);
    return ATLAS_OK;
}

/**
 * @brief Append one response line within caller capacity.
 * @param response Destination text.
 * @param capacity Destination capacity.
 * @param used Current byte count, updated on append.
 * @param line Line to append.
 * @return true on success or when no destination was requested.
 */
static bool atlas_rfd_append(char *response,
                             size_t capacity,
                             size_t *used,
                             const char *line)
{
    const size_t line_length = strlen(line);
    const size_t separator = (*used > 0U) ? 1U : 0U;

    if (response == NULL)
    {
        return true;
    }
    if ((*used + separator + line_length + 1U) > capacity)
    {
        return false;
    }
    if (separator != 0U)
    {
        response[(*used)++] = '\n';
    }
    memcpy(&response[*used], line, line_length);
    *used += line_length;
    response[*used] = '\0';
    return true;
}

/**
 * @brief Require at least one nonempty response line other than local command echo.
 * @param response Newline-delimited response text preceding terminal OK.
 * @param command Exact command whose optional echo must be ignored.
 * @return true when the modem supplied substantive response content.
 */
static bool atlas_rfd_has_non_echo_line(const char *response,
                                        const char *command)
{
    const size_t command_length = strlen(command);
    const char *cursor = response;

    if (response == NULL)
    {
        return false;
    }
    while (*cursor != '\0')
    {
        const char *end = strchr(cursor, '\n');
        const size_t line_length = (end == NULL) ? strlen(cursor) :
                                   (size_t)(end - cursor);
        if ((line_length != 0U) &&
            ((line_length != command_length) ||
             (memcmp(cursor, command, command_length) != 0)))
        {
            return true;
        }
        cursor = (end == NULL) ? (cursor + line_length) : (end + 1);
    }
    return false;
}

/**
 * @brief Parse a SiK S-register query response such as "S1: 115" or "115".
 * @param response Newline-delimited response text preceding terminal OK.
 * @param value Destination unsigned value.
 * @return true when exactly one trailing decimal value was parsed without overflow.
 */
static bool atlas_rfd_parse_register_value(const char *response,
                                           uint32_t *value)
{
    const char *cursor;
    const char *colon;
    const char *last_line;
    uint32_t parsed = 0U;
    bool have_digit = false;

    if ((response == NULL) || (value == NULL))
    {
        return false;
    }
    /* Parse only the final nonempty line so an enabled command echo is harmless. */
    last_line = strrchr(response, '\n');
    cursor = (last_line == NULL) ? response : (last_line + 1);
    if (*cursor == '\0')
    {
        return false;
    }
    colon = strrchr(cursor, ':');
    cursor = (colon == NULL) ? cursor : (colon + 1);
    while ((*cursor == ' ') || (*cursor == '\t'))
    {
        ++cursor;
    }
    while ((*cursor >= '0') && (*cursor <= '9'))
    {
        const uint32_t digit = (uint32_t)(*cursor - '0');
        if (parsed > ((UINT32_MAX - digit) / 10U))
        {
            return false;
        }
        parsed = (parsed * 10U) + digit;
        have_digit = true;
        ++cursor;
    }
    while ((*cursor == ' ') || (*cursor == '\t') || (*cursor == '\n'))
    {
        ++cursor;
    }
    if (!have_digit || (*cursor != '\0'))
    {
        return false;
    }
    *value = parsed;
    return true;
}

/**
 * @brief Wait for a terminal OK/ERROR line and collect preceding lines.
 * @param radio Radio driver.
 * @param response Optional response destination.
 * @param response_capacity Destination capacity.
 * @param timeout_ms Nonzero timeout.
 * @return ATLAS_OK, NACK, timeout, overflow, or transport failure.
 */
static AtlasStatus atlas_rfd_wait_terminal(AtlasRfd900x *radio,
                                           char *response,
                                           size_t response_capacity,
                                           uint32_t timeout_ms)
{
    char line[RFD_LINE_CAPACITY];
    size_t line_length = 0U;
    size_t response_used = 0U;
    uint8_t value;
    uint32_t started_ms = HAL_GetTick();
    AtlasStatus status;

    if (response != NULL)
    {
        response[0] = '\0';
    }
    while ((HAL_GetTick() - started_ms) < timeout_ms)
    {
        status = AtlasUartTransport_Service(radio->transport);
        if (status != ATLAS_OK)
        {
            return status;
        }
        while (AtlasUartTransport_ReadByte(radio->transport, &value))
        {
            if ((value == '\r') || (value == '\n'))
            {
                if (line_length == 0U)
                {
                    continue;
                }
                line[line_length] = '\0';
                line_length = 0U;
                if (strcmp(line, "OK") == 0)
                {
                    return ATLAS_OK;
                }
                if ((strcmp(line, "ERROR") == 0) ||
                    (strncmp(line, "ERROR", 5U) == 0))
                {
                    ++radio->health.command_errors;
                    return ATLAS_ERROR_NACK;
                }
                if (!atlas_rfd_append(response,
                                      response_capacity,
                                      &response_used,
                                      line))
                {
                    return ATLAS_ERROR_OVERFLOW;
                }
                continue;
            }
            if (line_length < (sizeof(line) - 1U))
            {
                line[line_length++] = (char)value;
            }
            else
            {
                return ATLAS_ERROR_OVERFLOW;
            }
        }
        AtlasTime_DelayMs(1U);
    }
    ++radio->health.command_timeouts;
    return ATLAS_ERROR_TIMEOUT;
}

/**
 * @brief Start USART3 transport without sending or changing any modem setting.
 * @param radio Destination driver instance.
 * @param transport Destination transport.
 * @param uart Initialized USART3 at the modem's current speed.
 * @return ATLAS_OK or a typed transport failure.
 */
AtlasStatus AtlasRfd900x_Init(AtlasRfd900x *radio,
                              AtlasUartTransport *transport,
                              UART_HandleTypeDef *uart)
{
    AtlasStatus status;

    if ((radio == NULL) || (transport == NULL) || (uart == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    memset(radio, 0, sizeof(*radio));
    radio->transport = transport;
    status = AtlasUartTransport_Init(transport, uart);
    if (status == ATLAS_OK)
    {
        status = AtlasUartTransport_Start(transport);
    }
    if (status == ATLAS_OK)
    {
        radio->initialized = true;
    }
    return status;
}

/**
 * @brief Write transparent flight-link bytes.
 * @param radio Initialized radio in transparent mode.
 * @param data Payload bytes.
 * @param length Byte count.
 * @param timeout_ms Nonzero transmit timeout.
 * @return ATLAS_OK or typed state/transport failure.
 */
AtlasStatus AtlasRfd900x_Write(AtlasRfd900x *radio,
                               const uint8_t *data,
                               size_t length,
                               uint32_t timeout_ms)
{
    AtlasStatus status;

    if ((radio == NULL) || (data == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!radio->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (radio->command_mode)
    {
        return ATLAS_ERROR_STATE;
    }
    status = AtlasUartTransport_Write(radio->transport, data, length, timeout_ms);
    if (status == ATLAS_OK)
    {
        radio->health.payload_bytes_written += (uint32_t)length;
    }
    return status;
}

/**
 * @brief Read currently buffered transparent flight-link bytes.
 * @param radio Initialized radio in transparent mode.
 * @param destination Destination bytes.
 * @param capacity Maximum byte count.
 * @return Number of bytes copied.
 */
size_t AtlasRfd900x_Read(AtlasRfd900x *radio,
                         uint8_t *destination,
                         size_t capacity)
{
    size_t count;

    if ((radio == NULL) || (destination == NULL) || !radio->initialized ||
        radio->command_mode)
    {
        return 0U;
    }
    count = AtlasUartTransport_Read(radio->transport, destination, capacity);
    radio->health.payload_bytes_read += (uint32_t)count;
    return count;
}

/**
 * @brief Enter local SiK command mode using the guarded "+++" sequence.
 * @param radio Initialized radio in transparent mode.
 * @return ATLAS_OK, BUSY when prior silence is absent, or typed timeout/transport failure.
 */
AtlasStatus AtlasRfd900x_EnterCommandMode(AtlasRfd900x *radio)
{
    static const uint8_t escape_sequence[3] = {'+', '+', '+'};
    AtlasStatus status;

    if (radio == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!radio->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (radio->command_mode)
    {
        return ATLAS_OK;
    }
    if ((HAL_GetTick() - AtlasUartTransport_LastActivityMs(radio->transport)) <
        RFD_GUARD_TIME_MS)
    {
        ++radio->health.guard_rejections;
        return ATLAS_ERROR_BUSY;
    }

    AtlasUartTransport_FlushRx(radio->transport);
    status = AtlasUartTransport_Write(radio->transport,
                                      escape_sequence,
                                      sizeof(escape_sequence),
                                      100U);
    if (status == ATLAS_OK)
    {
        /* The modem withholds OK until the post-escape guard interval completes. */
        status = atlas_rfd_wait_terminal(radio, NULL, 0U, RFD_ENTRY_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        radio->command_mode = true;
        ++radio->health.command_entries;
    }
    return status;
}

/**
 * @brief Execute one local SiK AT command and collect response lines.
 * @param radio Initialized radio in command mode.
 * @param command Command beginning with AT.
 * @param response Optional response destination.
 * @param response_capacity Destination capacity.
 * @param timeout_ms Nonzero timeout.
 * @return ATLAS_OK or a typed state, argument, transport, NACK, or timeout result.
 */
AtlasStatus AtlasRfd900x_Command(AtlasRfd900x *radio,
                                 const char *command,
                                 char *response,
                                 size_t response_capacity,
                                 uint32_t timeout_ms)
{
    uint8_t wire[RFD_COMMAND_CAPACITY + 1U];
    size_t length;
    AtlasStatus status;

    if ((radio == NULL) || (command == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    length = strlen(command);
    if (!radio->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (!radio->command_mode)
    {
        return ATLAS_ERROR_STATE;
    }
    if ((timeout_ms == 0U) || (length < 2U) ||
        (length > RFD_COMMAND_CAPACITY) || (strncmp(command, "AT", 2U) != 0) ||
        (strchr(command, '\r') != NULL) || (strchr(command, '\n') != NULL) ||
        ((response == NULL) != (response_capacity == 0U)))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    memcpy(wire, command, length);
    wire[length++] = '\r';
    AtlasUartTransport_FlushRx(radio->transport);
    status = AtlasUartTransport_Write(radio->transport, wire, length, 100U);
    if (status == ATLAS_OK)
    {
        ++radio->health.commands_sent;
        status = atlas_rfd_wait_terminal(radio,
                                         response,
                                         response_capacity,
                                         timeout_ms);
    }
    return status;
}

/**
 * @brief Exit local command mode and resume transparent operation.
 * @param radio Initialized radio in command mode.
 * @return ATLAS_OK or a typed state/transport failure.
 */
AtlasStatus AtlasRfd900x_ExitCommandMode(AtlasRfd900x *radio)
{
    AtlasStatus status;

    if (radio == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    status = AtlasRfd900x_Command(radio,
                                  "ATO",
                                  NULL,
                                  0U,
                                  RFD_COMMAND_TIMEOUT_MS);
    if (status == ATLAS_OK)
    {
        radio->command_mode = false;
        AtlasUartTransport_FlushRx(radio->transport);
    }
    return status;
}

/**
 * @brief Query modem identity with ATI.
 * @param radio Initialized radio in command mode.
 * @param identity Destination identity text.
 * @param identity_capacity Destination capacity.
 * @return ATLAS_OK only after terminal OK and a substantive non-echo line.
 */
AtlasStatus AtlasRfd900x_ReadIdentity(AtlasRfd900x *radio,
                                      char *identity,
                                      size_t identity_capacity)
{
    AtlasStatus status;

    if ((identity == NULL) || (identity_capacity == 0U))
    {
        return ATLAS_ERROR_NULL;
    }
    status = AtlasRfd900x_Command(radio,
                                  "ATI",
                                  identity,
                                  identity_capacity,
                                  RFD_COMMAND_TIMEOUT_MS);
    if ((status == ATLAS_OK) && !atlas_rfd_has_non_echo_line(identity, "ATI"))
    {
        ++radio->health.malformed_responses;
        status = ATLAS_ERROR_PROTOCOL;
    }
    return status;
}

/**
 * @brief Read the complete local S-register listing using ATI5.
 * @param radio Initialized radio in command mode.
 * @param settings Destination text.
 * @param settings_capacity Destination capacity.
 * @return ATLAS_OK only after terminal OK and a substantive non-echo line.
 */
AtlasStatus AtlasRfd900x_ReadSettings(AtlasRfd900x *radio,
                                      char *settings,
                                      size_t settings_capacity)
{
    AtlasStatus status;

    if ((settings == NULL) || (settings_capacity == 0U))
    {
        return ATLAS_ERROR_NULL;
    }
    status = AtlasRfd900x_Command(radio,
                                  "ATI5",
                                  settings,
                                  settings_capacity,
                                  RFD_COMMAND_TIMEOUT_MS);
    if ((status == ATLAS_OK) && !atlas_rfd_has_non_echo_line(settings, "ATI5"))
    {
        ++radio->health.malformed_responses;
        status = ATLAS_ERROR_PROTOCOL;
    }
    return status;
}

/**
 * @brief Change/read back one local SiK S-register and optionally save to NVM.
 * @param radio Initialized radio in command mode.
 * @param index S-register index.
 * @param value Register value.
 * @param persist true to issue AT&W.
 * @return ATLAS_OK only after all requested commands succeed.
 */
AtlasStatus AtlasRfd900x_SetParameter(AtlasRfd900x *radio,
                                      uint8_t index,
                                      uint32_t value,
                                      bool persist)
{
    char command[RFD_COMMAND_CAPACITY + 1U] = {0};
    char response[RFD_LINE_CAPACITY];
    uint32_t actual = 0U;
    AtlasStatus status;

    if (radio == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    status = atlas_rfd_parameter_line(command, sizeof(command), index, value, false);
    if (status == ATLAS_OK) status = AtlasRfd900x_Command(radio,
                                  command,
                                  NULL,
                                  0U,
                                  RFD_COMMAND_TIMEOUT_MS);
    if (status == ATLAS_OK)
    {
        status = atlas_rfd_parameter_line(command, sizeof(command), index, 0U, true);
        if (status == ATLAS_OK) status = AtlasRfd900x_Command(radio,
                                      command,
                                      response,
                                      sizeof(response),
                                      RFD_COMMAND_TIMEOUT_MS);
    }
    if ((status == ATLAS_OK) &&
        (!atlas_rfd_parse_register_value(response, &actual) || (actual != value)))
    {
        ++radio->health.configuration_mismatches;
        status = ATLAS_ERROR_PROTOCOL;
    }
    if ((status == ATLAS_OK) && persist)
    {
        status = AtlasRfd900x_Command(radio,
                                      "AT&W",
                                      NULL,
                                      0U,
                                      RFD_COMMAND_TIMEOUT_MS);
    }
    return status;
}

/**
 * @brief Explicitly change only the MCU-side USART3 baud rate.
 * @param radio Initialized radio instance.
 * @param baud_rate Baud already configured in the modem.
 * @return ATLAS_OK or a typed UART failure.
 */
AtlasStatus AtlasRfd900x_ReconfigureHostBaud(AtlasRfd900x *radio,
                                             uint32_t baud_rate)
{
    if (radio == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!radio->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    return AtlasUartTransport_ReconfigureBaud(radio->transport, baud_rate);
}
