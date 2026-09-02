/**
 * @file atlas_ble.c
 * @brief NINA-B112 reset control, strict AT line engine, and transparent byte access.
 *
 * Major functions:
 * - AtlasBle_Init(): verifies generic model and firmware identification commands.
 * - AtlasBle_Command(): tolerates unsolicited lines while requiring terminal OK/ERROR.
 * - AtlasBle_ConfigureSps(): separates volatile setup from explicit NVM persistence.
 * - AtlasBle_EnterCommandMode()/EnterDataMode(): use documented wired/AT transitions.
 * - AtlasBle_Reset(): guarantees normal boot strapping before releasing RESET_N.
 */

#include "atlas_ble.h"

#include "atlas_time.h"

#include <stdio.h>
#include <string.h>

#define BLE_RESET_ASSERT_MS  (20U)
#define BLE_BOOT_WAIT_MS     (800U)
#define BLE_COMMAND_TIMEOUT_MS (1500U)
#define BLE_MAX_COMMAND_LENGTH (96U)
#define BLE_MAX_NAME_LENGTH    (29U)
#define BLE_DSR_TRANSITION_MS   (20U)
#define BLE_MODE_TRANSITION_TIMEOUT_MS (1500U)
#define BLE_DATA_MODE_DELAY_MS  (50U)

/**
 * @brief Determine whether a line is a terminal AT error response.
 * @param line NUL-terminated response line.
 * @return true for ERROR, +CME ERROR, or +CMS ERROR.
 */
static bool atlas_ble_is_error_line(const char *line)
{
    return (strcmp(line, "ERROR") == 0) ||
           (strncmp(line, "+CME ERROR", 10U) == 0) ||
           (strncmp(line, "+CMS ERROR", 10U) == 0);
}

/**
 * @brief Append one response line without overrunning the caller's buffer.
 * @param ble Driver used for overflow accounting.
 * @param response Destination buffer.
 * @param capacity Destination capacity.
 * @param used Current bytes used, updated on success.
 * @param line Line to append.
 * @return true when the line was stored or no response was requested; false on overflow.
 */
static bool atlas_ble_append_response(AtlasBle *ble,
                                      char *response,
                                      size_t capacity,
                                      size_t *used,
                                      const char *line)
{
    const size_t line_length = strlen(line);
    const size_t separator = (*used > 0U) ? 1U : 0U;

    if ((response == NULL) || (capacity == 0U))
    {
        return true;
    }
    if ((*used + separator + line_length + 1U) > capacity)
    {
        ++ble->health.response_overflows;
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
 * @brief Validate a local BLE name before interpolating it into an AT command.
 * @param name Proposed local name.
 * @return true when safe and within the documented length bound.
 */
static bool atlas_ble_valid_name(const char *name)
{
    size_t index;
    size_t length;

    if (name == NULL)
    {
        return false;
    }
    length = strlen(name);
    if ((length == 0U) || (length > BLE_MAX_NAME_LENGTH))
    {
        return false;
    }
    for (index = 0U; index < length; ++index)
    {
        const unsigned char value = (unsigned char)name[index];
        if ((value < 0x20U) || (value > 0x7EU) ||
            (value == '"') || (value == ',') || (value == '\\'))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Test whether a newline-delimited AT response contains one exact line.
 * @param response NUL-terminated response text.
 * @param expected Exact line to locate.
 * @return true when expected occupies a complete response line.
 */
static bool atlas_ble_response_has_line(const char *response,
                                        const char *expected)
{
    const size_t expected_length = strlen(expected);
    const char *cursor = response;

    while ((cursor != NULL) && (*cursor != '\0'))
    {
        const char *end = strchr(cursor, '\n');
        const size_t line_length = (end == NULL) ? strlen(cursor) :
                                   (size_t)(end - cursor);
        if ((line_length == expected_length) &&
            (memcmp(cursor, expected, expected_length) == 0))
        {
            return true;
        }
        cursor = (end == NULL) ? NULL : (end + 1);
    }
    return false;
}

/**
 * @brief Read back every queryable SPS profile item and require an exact match.
 * @param ble Initialized driver in command mode.
 * @param device_name Expected local name.
 * @return ATLAS_OK or a typed command/protocol failure.
 * @note AT&D is set-only in u-connectXpress and is therefore verified separately
 *       by the wired transition performed after a persistent restart.
 */
static AtlasStatus atlas_ble_verify_sps_profile(AtlasBle *ble,
                                                const char *device_name)
{
    char response[ATLAS_BLE_LINE_CAPACITY];
    char expected[ATLAS_BLE_LINE_CAPACITY];
    char expected_quoted[ATLAS_BLE_LINE_CAPACITY];
    AtlasStatus status;

    status = AtlasBle_Command(ble, "AT+UBTLN?", response, sizeof(response),
                              BLE_COMMAND_TIMEOUT_MS);
    (void)snprintf(expected, sizeof(expected), "+UBTLN:%s", device_name);
    (void)snprintf(expected_quoted, sizeof(expected_quoted),
                   "+UBTLN:\"%s\"", device_name);
    if ((status == ATLAS_OK) &&
        !atlas_ble_response_has_line(response, expected) &&
        !atlas_ble_response_has_line(response, expected_quoted))
    {
        status = ATLAS_ERROR_PROTOCOL;
    }

    if (status == ATLAS_OK)
    {
        status = AtlasBle_Command(ble, "AT+UBTLE?", response, sizeof(response),
                                  BLE_COMMAND_TIMEOUT_MS);
        if ((status == ATLAS_OK) &&
            !atlas_ble_response_has_line(response, "+UBTLE:2"))
        {
            status = ATLAS_ERROR_PROTOCOL;
        }
    }
    if (status == ATLAS_OK)
    {
        status = AtlasBle_Command(ble, "AT+UDSC=0", response, sizeof(response),
                                  BLE_COMMAND_TIMEOUT_MS);
        if ((status == ATLAS_OK) &&
            !atlas_ble_response_has_line(response, "+UDSC:0,6"))
        {
            status = ATLAS_ERROR_PROTOCOL;
        }
    }
    if (status == ATLAS_OK)
    {
        status = AtlasBle_Command(ble, "AT+UMSM?", response, sizeof(response),
                                  BLE_COMMAND_TIMEOUT_MS);
        if ((status == ATLAS_OK) &&
            !atlas_ble_response_has_line(response, "+UMSM:1"))
        {
            status = ATLAS_ERROR_PROTOCOL;
        }
    }
    if (status == ATLAS_ERROR_PROTOCOL)
    {
        ++ble->health.configuration_mismatches;
    }
    return status;
}

/**
 * @brief Request command mode without relying on an in-band escape sequence.
 * @param ble Bound driver instance.
 * @note UART_DSR is a NINA input despite the board net name; AT&D1 uses a low-to-high
 *       transition to leave data mode while retaining any active SPS connection.
 */
static void atlas_ble_request_command_mode(AtlasBle *ble)
{
    AtlasUartTransport_FlushRx(ble->transport);
    HAL_GPIO_WritePin(BLE_DSR_GPIO_Port, BLE_DSR_Pin, GPIO_PIN_RESET);
    AtlasTime_DelayMs(BLE_DSR_TRANSITION_MS);
    HAL_GPIO_WritePin(BLE_DSR_GPIO_Port, BLE_DSR_Pin, GPIO_PIN_SET);
    AtlasTime_DelayMs(BLE_DSR_TRANSITION_MS);
}

/**
 * @brief Wait for the unsolicited terminal OK generated by an AT&D1 transition.
 * @param ble Bound driver instance with active UART reception.
 * @param timeout_ms Nonzero wait bound.
 * @return ATLAS_OK only for an exact terminal OK; otherwise a typed failure.
 * @note No AT bytes are sent here: a failed transition must not leak commands into data.
 */
static AtlasStatus atlas_ble_wait_mode_transition(AtlasBle *ble,
                                                  uint32_t timeout_ms)
{
    char line[ATLAS_BLE_LINE_CAPACITY];
    size_t line_length = 0U;
    uint32_t started_ms = HAL_GetTick();
    uint8_t value;
    AtlasStatus status;

    while ((HAL_GetTick() - started_ms) < timeout_ms)
    {
        status = AtlasUartTransport_Service(ble->transport);
        if (status != ATLAS_OK)
        {
            return status;
        }
        while (AtlasUartTransport_ReadByte(ble->transport, &value))
        {
            if (value == '\r')
            {
                continue;
            }
            if (value != '\n')
            {
                if (line_length >= (sizeof(line) - 1U))
                {
                    ++ble->health.response_overflows;
                    return ATLAS_ERROR_OVERFLOW;
                }
                line[line_length++] = (char)value;
                continue;
            }
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
            if (atlas_ble_is_error_line(line))
            {
                return ATLAS_ERROR_NACK;
            }
            /* Connection events may be interleaved; only terminal lines decide. */
        }
        AtlasTime_DelayMs(1U);
    }
    return ATLAS_ERROR_TIMEOUT;
}

/**
 * @brief Hardware-reset the module while preserving normal-mode switch levels.
 * @param ble Bound driver instance.
 * @return ATLAS_OK or ATLAS_ERROR_NULL/NOT_READY.
 */
AtlasStatus AtlasBle_Reset(AtlasBle *ble)
{
    if ((ble == NULL) || (ble->transport == NULL))
    {
        return ATLAS_ERROR_NULL;
    }

    /* Both switches high select normal application boot and avoid destructive modes. */
    HAL_GPIO_WritePin(BLE_SWITCH1_GPIO_Port, BLE_SWITCH1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BLE_SWITCH2_GPIO_Port, BLE_SWITCH2_Pin, GPIO_PIN_SET);
    /* Assert the NINA UART_DSR input before reset; later deassertion requests AT mode. */
    HAL_GPIO_WritePin(BLE_DSR_GPIO_Port, BLE_DSR_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BLE_RESET_N_GPIO_Port, BLE_RESET_N_Pin, GPIO_PIN_RESET);
    AtlasTime_DelayMs(BLE_RESET_ASSERT_MS);
    AtlasUartTransport_FlushRx(ble->transport);
    HAL_GPIO_WritePin(BLE_RESET_N_GPIO_Port, BLE_RESET_N_Pin, GPIO_PIN_SET);
    AtlasTime_DelayMs(BLE_BOOT_WAIT_MS);
    AtlasUartTransport_FlushRx(ble->transport);
    /* The saved UMSM profile owns the actual post-reset mode until it is probed. */
    ble->command_mode = false;
    ++ble->health.resets;
    return ATLAS_OK;
}

/**
 * @brief Execute one AT command and collect nonterminal response lines.
 * @param ble Bound driver instance in command mode.
 * @param command Command beginning with AT and containing no line ending.
 * @param response Optional response destination.
 * @param response_capacity Destination capacity.
 * @param timeout_ms Nonzero transaction timeout.
 * @return ATLAS_OK, NACK, timeout, overflow, or typed argument/state/transport failure.
 */
AtlasStatus AtlasBle_Command(AtlasBle *ble,
                             const char *command,
                             char *response,
                             size_t response_capacity,
                             uint32_t timeout_ms)
{
    char wire_command[BLE_MAX_COMMAND_LENGTH + 2U];
    char line[ATLAS_BLE_LINE_CAPACITY];
    size_t command_length;
    size_t line_length = 0U;
    size_t response_used = 0U;
    uint32_t started_ms;
    uint8_t value;
    AtlasStatus status;

    if ((ble == NULL) || (ble->transport == NULL) || (command == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    command_length = strlen(command);
    if ((!ble->command_mode) || (timeout_ms == 0U) ||
        (command_length < 2U) || (command_length > BLE_MAX_COMMAND_LENGTH) ||
        (strncmp(command, "AT", 2U) != 0) ||
        (strchr(command, '\r') != NULL) || (strchr(command, '\n') != NULL) ||
        ((response == NULL) != (response_capacity == 0U)))
    {
        return !ble->command_mode ? ATLAS_ERROR_STATE : ATLAS_ERROR_ARGUMENT;
    }
    if (response != NULL)
    {
        response[0] = '\0';
    }

    memcpy(wire_command, command, command_length);
    wire_command[command_length] = '\r';
    wire_command[command_length + 1U] = '\n';
    AtlasUartTransport_FlushRx(ble->transport);
    status = AtlasUartTransport_Write(ble->transport,
                                      (const uint8_t *)wire_command,
                                      command_length + 2U,
                                      100U);
    if (status != ATLAS_OK)
    {
        return status;
    }
    ++ble->health.commands_sent;

    started_ms = HAL_GetTick();
    while ((HAL_GetTick() - started_ms) < timeout_ms)
    {
        status = AtlasUartTransport_Service(ble->transport);
        if (status != ATLAS_OK)
        {
            return status;
        }
        while (AtlasUartTransport_ReadByte(ble->transport, &value))
        {
            if (value == '\r')
            {
                continue;
            }
            if (value != '\n')
            {
                if (line_length >= (sizeof(line) - 1U))
                {
                    /* Never accept a terminal OK after discarding response bytes. */
                    ++ble->health.response_overflows;
                    return ATLAS_ERROR_OVERFLOW;
                }
                line[line_length++] = (char)value;
                continue;
            }
            if (line_length == 0U)
            {
                continue;
            }
            line[line_length] = '\0';
            line_length = 0U;

            if (strcmp(line, "OK") == 0)
            {
                ++ble->health.command_ok;
                return ATLAS_OK;
            }
            if (atlas_ble_is_error_line(line))
            {
                ++ble->health.command_errors;
                return ATLAS_ERROR_NACK;
            }
            /* Ignore local echo but preserve identity and unsolicited result lines. */
            if (strcmp(line, command) != 0)
            {
                if (!atlas_ble_append_response(ble,
                                               response,
                                               response_capacity,
                                               &response_used,
                                               line))
                {
                    return ATLAS_ERROR_OVERFLOW;
                }
            }
        }
        AtlasTime_DelayMs(1U);
    }

    ++ble->health.command_timeouts;
    return ATLAS_ERROR_TIMEOUT;
}

/**
 * @brief Boot NINA-B112 in normal mode and prove two-way AT communication.
 * @param ble Destination driver instance.
 * @param transport Destination UART transport object.
 * @param uart Initialized USART6 at 115200 8N1 with RTS/CTS.
 * @return ATLAS_OK or a typed transport, timeout, or identity failure.
 */
AtlasStatus AtlasBle_Init(AtlasBle *ble,
                          AtlasUartTransport *transport,
                          UART_HandleTypeDef *uart)
{
    AtlasStatus status;

    if ((ble == NULL) || (transport == NULL) || (uart == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    memset(ble, 0, sizeof(*ble));
    ble->transport = transport;

    status = AtlasUartTransport_Init(transport, uart);
    if (status == ATLAS_OK)
    {
        status = AtlasUartTransport_Start(transport);
    }
    if (status == ATLAS_OK)
    {
        status = AtlasBle_Reset(ble);
    }
    if (status == ATLAS_OK)
    {
        /* A Data-mode startup emits OK after AT&D1; Command-mode startup emits nothing. */
        atlas_ble_request_command_mode(ble);
        status = atlas_ble_wait_mode_transition(ble,
                                                BLE_MODE_TRANSITION_TIMEOUT_MS);
        if (status == ATLAS_ERROR_TIMEOUT)
        {
            /* No transition response is expected when the saved profile already starts in AT mode. */
            status = ATLAS_OK;
        }
        else if (status != ATLAS_OK)
        {
            ++ble->health.mode_transition_failures;
        }
    }
    if (status == ATLAS_OK)
    {
        /* Probe only after the transition window, so AT cannot be emitted into data mode early. */
        ble->command_mode = true;
        status = AtlasBle_Command(ble, "AT", NULL, 0U, BLE_COMMAND_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        status = AtlasBle_Command(ble,
                                  "AT+CGMM",
                                  ble->model,
                                  sizeof(ble->model),
                                  BLE_COMMAND_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        status = AtlasBle_Command(ble,
                                  "AT+CGMR",
                                  ble->firmware,
                                  sizeof(ble->firmware),
                                  BLE_COMMAND_TIMEOUT_MS);
    }
    if ((status != ATLAS_OK) || (ble->model[0] == '\0') ||
        (ble->firmware[0] == '\0'))
    {
        return (status == ATLAS_OK) ? ATLAS_ERROR_IDENTITY : status;
    }

    ble->initialized = true;
    return ATLAS_OK;
}

/**
 * @brief Configure BLE peripheral role, local name, and SPS startup mode.
 * @param ble Initialized driver instance.
 * @param device_name Validated local name.
 * @param persist true to write NVM and restart.
 * @return ATLAS_OK only when every command succeeds.
 */
AtlasStatus AtlasBle_ConfigureSps(AtlasBle *ble,
                                  const char *device_name,
                                  bool persist)
{
    char command[BLE_MAX_COMMAND_LENGTH + 1U];
    AtlasStatus status;

    if ((ble == NULL) || (device_name == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!ble->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (!atlas_ble_valid_name(device_name))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    (void)snprintf(command, sizeof(command), "AT+UBTLN=\"%s\"", device_name);
    status = AtlasBle_Command(ble, command, NULL, 0U, BLE_COMMAND_TIMEOUT_MS);
    if (status == ATLAS_OK)
    {
        status = AtlasBle_Command(ble, "AT+UBTLE=2", NULL, 0U,
                                  BLE_COMMAND_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        /* Replace any server-zero definition with the documented SPS server type. */
        status = AtlasBle_Command(ble, "AT+UDSC=0,0", NULL, 0U,
                                  BLE_COMMAND_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        status = AtlasBle_Command(ble, "AT+UDSC=0,6", NULL, 0U,
                                  BLE_COMMAND_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        /* UMSM=1 is ordinary transparent Data mode; UMSM=2 would select EDM. */
        status = AtlasBle_Command(ble, "AT+UMSM=1", NULL, 0U,
                                  BLE_COMMAND_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        /* Fix the wired DSR transition semantics used by AtlasBle_EnterCommandMode(). */
        status = AtlasBle_Command(ble, "AT&D1", NULL, 0U,
                                  BLE_COMMAND_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_ble_verify_sps_profile(ble, device_name);
    }
    if ((status != ATLAS_OK) || !persist)
    {
        return status;
    }

    status = AtlasBle_Command(ble, "AT&W", NULL, 0U, BLE_COMMAND_TIMEOUT_MS);
    if (status == ATLAS_OK)
    {
        status = AtlasBle_Command(ble, "AT+CPWROFF", NULL, 0U,
                                  BLE_COMMAND_TIMEOUT_MS);
    }
    if (status == ATLAS_OK)
    {
        /* RESET_N supplies the documented wake/restart path after CPWROFF. */
        status = AtlasBle_Reset(ble);
    }
    if (status == ATLAS_OK)
    {
        /* Prove the persisted AT&D1 path before trusting any post-reset profile. */
        status = AtlasBle_EnterCommandMode(ble);
    }
    if (status == ATLAS_OK)
    {
        status = atlas_ble_verify_sps_profile(ble, device_name);
    }
    if (status == ATLAS_OK)
    {
        /* Return to the committed UMSM=1 operating mode only after full readback. */
        status = AtlasBle_EnterDataMode(ble);
    }
    return status;
}

/**
 * @brief Enter command mode through the wired DSR control and verify with AT.
 * @param ble Initialized driver instance.
 * @return ATLAS_OK only after the module replies OK.
 */
AtlasStatus AtlasBle_EnterCommandMode(AtlasBle *ble)
{
    AtlasStatus status;

    if (ble == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!ble->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (ble->command_mode)
    {
        return ATLAS_OK;
    }

    atlas_ble_request_command_mode(ble);
    status = atlas_ble_wait_mode_transition(ble,
                                            BLE_MODE_TRANSITION_TIMEOUT_MS);
    if (status == ATLAS_OK)
    {
        ble->command_mode = true;
        status = AtlasBle_Command(ble, "AT", NULL, 0U, BLE_COMMAND_TIMEOUT_MS);
    }
    if (status != ATLAS_OK)
    {
        ble->command_mode = false;
        ++ble->health.mode_transition_failures;
        return status;
    }
    ++ble->health.mode_transitions;
    return ATLAS_OK;
}

/**
 * @brief Enter ordinary transparent data mode using ATO1.
 * @param ble Initialized driver instance in command mode.
 * @return ATLAS_OK only after the module acknowledges the transition.
 */
AtlasStatus AtlasBle_EnterDataMode(AtlasBle *ble)
{
    AtlasStatus status;

    if (ble == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!ble->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (!ble->command_mode)
    {
        return ATLAS_OK;
    }

    status = AtlasBle_Command(ble, "ATO1", NULL, 0U, BLE_COMMAND_TIMEOUT_MS);
    if (status == ATLAS_OK)
    {
        ble->command_mode = false;
        ++ble->health.mode_transitions;
        /* u-connectXpress requires 50 ms before transmitting mode data. */
        AtlasTime_DelayMs(BLE_DATA_MODE_DELAY_MS);
    }
    else
    {
        ++ble->health.mode_transition_failures;
    }
    return status;
}

/**
 * @brief Write transparent payload bytes while the module is in data mode.
 * @param ble Initialized driver instance.
 * @param data Payload bytes.
 * @param length Byte count.
 * @param timeout_ms Nonzero transmit timeout.
 * @return ATLAS_OK or a typed state/transport failure.
 */
AtlasStatus AtlasBle_WriteData(AtlasBle *ble,
                               const uint8_t *data,
                               size_t length,
                               uint32_t timeout_ms)
{
    AtlasStatus status;

    if ((ble == NULL) || (data == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if (!ble->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (ble->command_mode)
    {
        return ATLAS_ERROR_STATE;
    }
    status = AtlasUartTransport_Write(ble->transport, data, length, timeout_ms);
    if (status == ATLAS_OK)
    {
        ble->health.data_bytes_written += (uint32_t)length;
    }
    return status;
}

/**
 * @brief Read currently buffered transparent payload bytes.
 * @param ble Initialized driver instance.
 * @param destination Destination buffer.
 * @param capacity Maximum byte count.
 * @return Number of bytes copied.
 */
size_t AtlasBle_ReadData(AtlasBle *ble,
                         uint8_t *destination,
                         size_t capacity)
{
    size_t count;

    if ((ble == NULL) || (destination == NULL) || !ble->initialized ||
        ble->command_mode)
    {
        return 0U;
    }
    count = AtlasUartTransport_Read(ble->transport, destination, capacity);
    ble->health.data_bytes_read += (uint32_t)count;
    return count;
}

/**
 * @brief Read the active-low module DTR/connection-control signal.
 * @param ble Driver instance.
 * @return true when the physical DTR input to the MCU is low.
 */
bool AtlasBle_IsDtrAsserted(const AtlasBle *ble)
{
    if (ble == NULL)
    {
        return false;
    }
    return HAL_GPIO_ReadPin(BLE_DTR_GPIO_Port, BLE_DTR_Pin) == GPIO_PIN_RESET;
}
