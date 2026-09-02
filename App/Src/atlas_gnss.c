/**
 * @file atlas_gnss.c
 * @brief UBX framing, NEO-M9N volatile configuration, NAV-PVT decoding, and PPS capture.
 *
 * Major functions:
 * - AtlasGnss_Init(): validates module identity using a MON-VER poll/response.
 * - AtlasGnss_SendUbx(): frames commands, calculates checksum, and correlates ACKs.
 * - AtlasGnss_ConfigureRam(): applies non-persistent CFG-VALSET keys safely.
 * - AtlasGnss_Service(): resynchronizes after noise and validates every UBX checksum.
 */

#include "atlas_gnss.h"

#include "atlas_time.h"

#include <string.h>

#define UBX_SYNC_1                      (0xB5U)
#define UBX_SYNC_2                      (0x62U)
#define UBX_PPS_SNAPSHOT_ATTEMPTS          (3U)
#define UBX_CLASS_NAV                   (0x01U)
#define UBX_ID_NAV_PVT                  (0x07U)
#define UBX_CLASS_ACK                   (0x05U)
#define UBX_ID_ACK_NAK                  (0x00U)
#define UBX_ID_ACK_ACK                  (0x01U)
#define UBX_CLASS_CFG                   (0x06U)
#define UBX_ID_CFG_VALSET               (0x8AU)
#define UBX_ID_CFG_VALGET               (0x8BU)
#define UBX_CLASS_MON                   (0x0AU)
#define UBX_ID_MON_VER                  (0x04U)
#define UBX_NAV_PVT_LENGTH              (92U)
#define UBX_MON_VER_MIN_LENGTH          (40U)
#define UBX_FRAME_OVERHEAD              (8U)
#define UBX_DEFAULT_TIMEOUT_MS          (1000U)
#define UBX_INTERBYTE_TIMEOUT_MS         (250U)
#define UBX_FRAME_TIMEOUT_MS            (2000U)

#define UBX_CFG_LAYER_RAM               (0x01U)
#define UBX_KEY_RATE_MEAS               (0x30210001UL)
#define UBX_KEY_RATE_NAV                (0x30210002UL)
#define UBX_KEY_RATE_TIMEREF            (0x20210003UL)
#define UBX_KEY_UART1OUTPROT_UBX        (0x10740001UL)
#define UBX_KEY_UART1OUTPROT_NMEA       (0x10740002UL)
#define UBX_KEY_MSGOUT_NAV_PVT_UART1    (0x20910007UL)

/**
 * @brief Read a little-endian unsigned 16-bit value.
 * @param data At least two bytes.
 * @return Decoded value.
 */
static uint16_t atlas_gnss_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/**
 * @brief Read a little-endian unsigned 32-bit value.
 * @param data At least four bytes.
 * @return Decoded value.
 */
static uint32_t atlas_gnss_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

/**
 * @brief Read a little-endian signed 32-bit value without alignment assumptions.
 * @param data At least four bytes.
 * @return Decoded value.
 */
static int32_t atlas_gnss_i32(const uint8_t *data)
{
    return (int32_t)atlas_gnss_u32(data);
}

/**
 * @brief Return the encoded value width for a u-blox configuration key.
 * @param key Complete configuration key ID.
 * @return Value width in bytes, or zero for an unsupported/invalid type code.
 */
static size_t atlas_gnss_key_value_size(uint32_t key)
{
    switch ((key >> 28) & 0x0FU)
    {
        case 1U: /* L */
        case 2U: /* U1, I1, E1, X1 */
            return 1U;
        case 3U: /* U2, I2, E2, X2 */
            return 2U;
        case 4U: /* U4, I4, R4, E4, X4 */
            return 4U;
        case 5U: /* U8, I8, R8, X8 */
            return 8U;
        default:
            return 0U;
    }
}

/**
 * @brief Decode up to eight little-endian configuration value bytes.
 * @param data Value bytes.
 * @param size Width from one through eight bytes.
 * @return Unsigned decoded representation.
 */
static uint64_t atlas_gnss_u64_width(const uint8_t *data, size_t size)
{
    uint64_t value = 0U;
    size_t index;

    for (index = 0U; index < size; ++index)
    {
        value |= (uint64_t)data[index] << (8U * index);
    }
    return value;
}

/**
 * @brief Add one byte to the rolling UBX checksum.
 * @param gnss Parser instance.
 * @param value Byte covered by checksum.
 */
static void atlas_gnss_checksum_add(AtlasGnss *gnss, uint8_t value)
{
    gnss->checksum_a = (uint8_t)(gnss->checksum_a + value);
    gnss->checksum_b = (uint8_t)(gnss->checksum_b + gnss->checksum_a);
}

/**
 * @brief Copy fixed-width printable u-blox version text and terminate it.
 * @param destination Destination C string.
 * @param source Fixed-width source bytes.
 * @param source_length Source field width.
 */
static void atlas_gnss_copy_version(char destination[ATLAS_GNSS_VERSION_TEXT_CAPACITY],
                                    const uint8_t *source,
                                    size_t source_length)
{
    size_t index;
    const size_t limit = (source_length < (ATLAS_GNSS_VERSION_TEXT_CAPACITY - 1U)) ?
                         source_length : (ATLAS_GNSS_VERSION_TEXT_CAPACITY - 1U);

    for (index = 0U; index < limit; ++index)
    {
        const uint8_t value = source[index];
        if (value == 0U)
        {
            break;
        }
        destination[index] = ((value >= 0x20U) && (value <= 0x7EU)) ? (char)value : '?';
    }
    destination[index] = '\0';
}

/**
 * @brief Decode a checksum-verified NAV-PVT payload.
 * @param gnss Driver instance.
 */
static void atlas_gnss_decode_nav_pvt(AtlasGnss *gnss)
{
    AtlasGnssNavPvt *nav = &gnss->latest_nav;
    const uint8_t *p = gnss->payload;

    if (gnss->payload_length != UBX_NAV_PVT_LENGTH)
    {
        ++gnss->health.malformed_nav_pvt;
        return;
    }

    nav->time_of_week_ms = atlas_gnss_u32(&p[0]);
    nav->year = atlas_gnss_u16(&p[4]);
    nav->month = p[6];
    nav->day = p[7];
    nav->hour = p[8];
    nav->minute = p[9];
    nav->second = p[10];
    nav->valid_flags = p[11];
    nav->time_accuracy_ns = atlas_gnss_u32(&p[12]);
    nav->nanoseconds = atlas_gnss_i32(&p[16]);
    nav->fix_type = p[20];
    nav->flags = p[21];
    nav->flags2 = p[22];
    nav->satellites_used = p[23];
    nav->longitude_1e7_deg = atlas_gnss_i32(&p[24]);
    nav->latitude_1e7_deg = atlas_gnss_i32(&p[28]);
    nav->height_ellipsoid_mm = atlas_gnss_i32(&p[32]);
    nav->height_msl_mm = atlas_gnss_i32(&p[36]);
    nav->horizontal_accuracy_mm = atlas_gnss_u32(&p[40]);
    nav->vertical_accuracy_mm = atlas_gnss_u32(&p[44]);
    nav->velocity_north_mm_s = atlas_gnss_i32(&p[48]);
    nav->velocity_east_mm_s = atlas_gnss_i32(&p[52]);
    nav->velocity_down_mm_s = atlas_gnss_i32(&p[56]);
    nav->ground_speed_mm_s = atlas_gnss_i32(&p[60]);
    nav->heading_motion_1e5_deg = atlas_gnss_i32(&p[64]);
    nav->speed_accuracy_mm_s = atlas_gnss_u32(&p[68]);
    nav->heading_accuracy_1e5_deg = atlas_gnss_u32(&p[72]);
    nav->position_dop_0p01 = atlas_gnss_u16(&p[76]);
    nav->flags3 = p[78];
    nav->heading_vehicle_1e5_deg = atlas_gnss_i32(&p[84]);
    nav->magnetic_declination_1e2_deg = (int16_t)atlas_gnss_u16(&p[88]);
    nav->magnetic_accuracy_1e2_deg = atlas_gnss_u16(&p[90]);
    nav->received_at_ms = HAL_GetTick();

    gnss->nav_available = true;
    ++gnss->health.nav_pvt_frames;
}

/**
 * @brief Dispatch one checksum-verified UBX frame.
 * @param gnss Driver instance.
 */
static void atlas_gnss_dispatch_frame(AtlasGnss *gnss)
{
    ++gnss->health.valid_ubx_frames;

    if ((gnss->message_class == UBX_CLASS_NAV) &&
        (gnss->message_id == UBX_ID_NAV_PVT))
    {
        atlas_gnss_decode_nav_pvt(gnss);
    }
    else if ((gnss->message_class == UBX_CLASS_ACK) &&
             ((gnss->message_id == UBX_ID_ACK_ACK) ||
              (gnss->message_id == UBX_ID_ACK_NAK)) &&
             (gnss->payload_length == 2U))
    {
        if (gnss->ack_pending && (gnss->payload[0] == gnss->ack_class) &&
            (gnss->payload[1] == gnss->ack_id))
        {
            gnss->ack_was_nak = (gnss->message_id == UBX_ID_ACK_NAK);
            gnss->ack_received = true;
            if (gnss->ack_was_nak)
            {
                ++gnss->health.negative_acknowledgements;
            }
            else
            {
                ++gnss->health.acknowledgements;
            }
        }
        else if (gnss->config_poll_pending &&
                 (gnss->payload[0] == UBX_CLASS_CFG) &&
                 (gnss->payload[1] == UBX_ID_CFG_VALGET) &&
                 (gnss->message_id == UBX_ID_ACK_NAK))
        {
            gnss->config_poll_was_nak = true;
            gnss->config_poll_received = true;
            ++gnss->health.negative_acknowledgements;
        }
    }
    else if ((gnss->message_class == UBX_CLASS_CFG) &&
             (gnss->message_id == UBX_ID_CFG_VALGET) &&
             gnss->config_poll_pending &&
             (gnss->payload_length >= 4U) &&
             (gnss->payload_length <= ATLAS_GNSS_CONFIG_RESPONSE_CAPACITY))
    {
        memcpy(gnss->config_response, gnss->payload, gnss->payload_length);
        gnss->config_response_length = gnss->payload_length;
        gnss->config_poll_received = true;
    }
    else if ((gnss->message_class == UBX_CLASS_MON) &&
             (gnss->message_id == UBX_ID_MON_VER) &&
             (gnss->payload_length >= UBX_MON_VER_MIN_LENGTH))
    {
        atlas_gnss_copy_version(gnss->software_version, &gnss->payload[0], 30U);
        atlas_gnss_copy_version(gnss->hardware_version, &gnss->payload[30], 10U);
        gnss->version_received = true;
    }
}

/**
 * @brief Reset the byte parser, preserving a possible first sync byte.
 * @param gnss Driver instance.
 * @param last_byte Last byte observed.
 */
static void atlas_gnss_resync(AtlasGnss *gnss, uint8_t last_byte)
{
    gnss->parser_state = (last_byte == UBX_SYNC_1) ?
                         ATLAS_GNSS_PARSE_SYNC2 : ATLAS_GNSS_PARSE_SYNC1;
    gnss->payload_index = 0U;
    gnss->discard_remaining = 0U;
    gnss->frame_started_ms = HAL_GetTick();
    gnss->last_byte_ms = gnss->frame_started_ms;
}

/**
 * @brief Abandon a frame whose stream continuity or bounded age was lost.
 * @param gnss Parser with an initialized transport.
 * @param now_ms Current monotonic service time.
 * @return true when unlocatable UART loss required flushing the receive ring.
 * @note UART bytes have no per-byte timestamps. Long service gaps deliberately
 *       invalidate a partial frame; a new complete frame must re-establish sync.
 */
static bool atlas_gnss_check_continuity(AtlasGnss *gnss, uint32_t now_ms)
{
    const uint32_t dropped = gnss->transport->health.dropped_bytes;
    const uint32_t restarts = gnss->transport->health.receive_restarts;
    if ((dropped != gnss->observed_dropped_bytes) ||
        (restarts != gnss->observed_receive_restarts))
    {
        gnss->observed_dropped_bytes = dropped;
        gnss->observed_receive_restarts = restarts;
        ++gnss->health.transport_resynchronizations;
        atlas_gnss_resync(gnss, 0U);
        /* We cannot locate the missing bytes within this queue. Do not combine
         * a pre-loss prefix with a post-loss suffix and publish a spliced frame. */
        AtlasUartTransport_FlushRx(gnss->transport);
        return true;
    }
    if ((gnss->parser_state != ATLAS_GNSS_PARSE_SYNC1) &&
        (((uint32_t)(now_ms - gnss->last_byte_ms) >= UBX_INTERBYTE_TIMEOUT_MS) ||
         ((uint32_t)(now_ms - gnss->frame_started_ms) >= UBX_FRAME_TIMEOUT_MS)))
    {
        ++gnss->health.frame_timeouts;
        atlas_gnss_resync(gnss, 0U);
    }
    return false;
}

/**
 * @brief Consume one byte through the UBX finite-state parser.
 * @param gnss Driver instance.
 * @param value Received byte.
 */
static void atlas_gnss_parse_byte(AtlasGnss *gnss, uint8_t value)
{
    ++gnss->health.bytes_parsed;

    switch (gnss->parser_state)
    {
        case ATLAS_GNSS_PARSE_SYNC1:
            if (value == UBX_SYNC_1)
            {
                gnss->frame_started_ms = HAL_GetTick();
                gnss->parser_state = ATLAS_GNSS_PARSE_SYNC2;
            }
            break;

        case ATLAS_GNSS_PARSE_SYNC2:
            if (value == UBX_SYNC_2)
            {
                gnss->checksum_a = 0U;
                gnss->checksum_b = 0U;
                gnss->parser_state = ATLAS_GNSS_PARSE_CLASS;
            }
            else
            {
                atlas_gnss_resync(gnss, value);
            }
            break;

        case ATLAS_GNSS_PARSE_CLASS:
            gnss->message_class = value;
            atlas_gnss_checksum_add(gnss, value);
            gnss->parser_state = ATLAS_GNSS_PARSE_ID;
            break;

        case ATLAS_GNSS_PARSE_ID:
            gnss->message_id = value;
            atlas_gnss_checksum_add(gnss, value);
            gnss->parser_state = ATLAS_GNSS_PARSE_LENGTH1;
            break;

        case ATLAS_GNSS_PARSE_LENGTH1:
            gnss->payload_length = value;
            atlas_gnss_checksum_add(gnss, value);
            gnss->parser_state = ATLAS_GNSS_PARSE_LENGTH2;
            break;

        case ATLAS_GNSS_PARSE_LENGTH2:
            gnss->payload_length |= (uint16_t)((uint16_t)value << 8);
            atlas_gnss_checksum_add(gnss, value);
            gnss->payload_index = 0U;
            if (gnss->payload_length > ATLAS_GNSS_MAX_UBX_PAYLOAD)
            {
                ++gnss->health.oversize_frames;
                /* A corrupt length is not a trustworthy discard budget. Resume
                 * bounded sync scanning immediately; never wait for 65535 bytes.
                 * A candidate still needs a valid checksum and supported shape. */
                atlas_gnss_resync(gnss, value);
            }
            else
            {
                gnss->parser_state = (gnss->payload_length == 0U) ?
                                     ATLAS_GNSS_PARSE_CHECKSUM_A :
                                     ATLAS_GNSS_PARSE_PAYLOAD;
            }
            break;

        case ATLAS_GNSS_PARSE_PAYLOAD:
            gnss->payload[gnss->payload_index++] = value;
            atlas_gnss_checksum_add(gnss, value);
            if (gnss->payload_index >= gnss->payload_length)
            {
                gnss->parser_state = ATLAS_GNSS_PARSE_CHECKSUM_A;
            }
            break;

        case ATLAS_GNSS_PARSE_CHECKSUM_A:
            gnss->received_checksum_a = value;
            gnss->parser_state = ATLAS_GNSS_PARSE_CHECKSUM_B;
            break;

        case ATLAS_GNSS_PARSE_CHECKSUM_B:
            if ((gnss->received_checksum_a == gnss->checksum_a) &&
                (value == gnss->checksum_b))
            {
                atlas_gnss_dispatch_frame(gnss);
            }
            else
            {
                ++gnss->health.checksum_errors;
            }
            atlas_gnss_resync(gnss, value);
            break;

        case ATLAS_GNSS_PARSE_DISCARD:
            if (gnss->discard_remaining > 0U)
            {
                --gnss->discard_remaining;
            }
            if (gnss->discard_remaining == 0U)
            {
                atlas_gnss_resync(gnss, value);
            }
            break;

        default:
            atlas_gnss_resync(gnss, value);
            break;
    }
}

/**
 * @brief Write a little-endian 32-bit key into a VALSET payload.
 * @param destination Four-byte destination.
 * @param value Value to encode.
 */
static void atlas_gnss_put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

/**
 * @brief Append a U1 configuration key/value pair.
 * @param payload VALSET payload.
 * @param index Current write index, updated on success.
 * @param key u-blox configuration key.
 * @param value U1 value.
 */
static void atlas_gnss_append_u1(uint8_t *payload,
                                 size_t *index,
                                 uint32_t key,
                                 uint8_t value)
{
    atlas_gnss_put_u32(&payload[*index], key);
    *index += 4U;
    payload[(*index)++] = value;
}

/**
 * @brief Append a U2 configuration key/value pair.
 * @param payload VALSET payload.
 * @param index Current write index, updated on success.
 * @param key u-blox configuration key.
 * @param value U2 value.
 */
static void atlas_gnss_append_u2(uint8_t *payload,
                                 size_t *index,
                                 uint32_t key,
                                 uint16_t value)
{
    atlas_gnss_put_u32(&payload[*index], key);
    *index += 4U;
    payload[(*index)++] = (uint8_t)value;
    payload[(*index)++] = (uint8_t)(value >> 8);
}

/** @brief One expected RAM-layer configuration key/value pair. */
typedef struct
{
    uint32_t key;
    uint64_t value;
    bool seen;
} AtlasGnssExpectedConfig;

/**
 * @brief Require the VALGET response to contain each requested value exactly once.
 * @param gnss Driver holding the checksum-verified CFG-VALGET payload.
 * @param measurement_period_ms Requested measurement period.
 * @param disable_nmea Requested UART1 NMEA state.
 * @return ATLAS_OK or ATLAS_ERROR_PROTOCOL.
 */
static AtlasStatus atlas_gnss_validate_ram_configuration(AtlasGnss *gnss,
                                                         uint16_t measurement_period_ms,
                                                         bool disable_nmea)
{
    AtlasGnssExpectedConfig expected[] = {
        {UBX_KEY_RATE_MEAS, measurement_period_ms, false},
        {UBX_KEY_RATE_NAV, 1U, false},
        {UBX_KEY_RATE_TIMEREF, 0U, false},
        {UBX_KEY_UART1OUTPROT_UBX, 1U, false},
        {UBX_KEY_UART1OUTPROT_NMEA, disable_nmea ? 0U : 1U, false},
        {UBX_KEY_MSGOUT_NAV_PVT_UART1, 1U, false}
    };
    size_t response_index = 4U;
    size_t expected_index;

    if ((gnss->config_response_length < 4U) ||
        (gnss->config_response[0] != 1U) || /* VALGET response version. */
        (gnss->config_response[1] != 0U) || /* RAM layer. */
        (atlas_gnss_u16(&gnss->config_response[2]) != 0U))
    {
        return ATLAS_ERROR_PROTOCOL;
    }

    while (response_index < gnss->config_response_length)
    {
        uint32_t key;
        size_t value_size;
        bool matched = false;

        if ((gnss->config_response_length - response_index) < 4U)
        {
            return ATLAS_ERROR_PROTOCOL;
        }
        key = atlas_gnss_u32(&gnss->config_response[response_index]);
        response_index += 4U;
        value_size = atlas_gnss_key_value_size(key);
        if ((value_size == 0U) ||
            ((gnss->config_response_length - response_index) < value_size))
        {
            return ATLAS_ERROR_PROTOCOL;
        }

        for (expected_index = 0U;
             expected_index < (sizeof(expected) / sizeof(expected[0]));
             ++expected_index)
        {
            if (expected[expected_index].key == key)
            {
                if (expected[expected_index].seen ||
                    (atlas_gnss_u64_width(&gnss->config_response[response_index],
                                          value_size) != expected[expected_index].value))
                {
                    return ATLAS_ERROR_PROTOCOL;
                }
                expected[expected_index].seen = true;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            return ATLAS_ERROR_PROTOCOL;
        }
        response_index += value_size;
    }

    for (expected_index = 0U;
         expected_index < (sizeof(expected) / sizeof(expected[0]));
         ++expected_index)
    {
        if (!expected[expected_index].seen)
        {
            return ATLAS_ERROR_PROTOCOL;
        }
    }
    return ATLAS_OK;
}

/**
 * @brief Poll and validate all RAM keys written by AtlasGnss_ConfigureRam().
 * @param gnss Initialized GNSS instance.
 * @param measurement_period_ms Requested measurement period.
 * @param disable_nmea Requested UART1 NMEA state.
 * @return ATLAS_OK, NACK, timeout, transport failure, or protocol mismatch.
 */
static AtlasStatus atlas_gnss_readback_ram_configuration(AtlasGnss *gnss,
                                                         uint16_t measurement_period_ms,
                                                         bool disable_nmea)
{
    const uint32_t keys[] = {
        UBX_KEY_RATE_MEAS,
        UBX_KEY_RATE_NAV,
        UBX_KEY_RATE_TIMEREF,
        UBX_KEY_UART1OUTPROT_UBX,
        UBX_KEY_UART1OUTPROT_NMEA,
        UBX_KEY_MSGOUT_NAV_PVT_UART1
    };
    uint8_t payload[4U + sizeof(keys)] = {0U};
    uint32_t started_ms;
    size_t index;
    AtlasStatus status;

    payload[0] = 0U; /* VALGET request version. */
    payload[1] = 0U; /* Read effective RAM layer. */
    for (index = 0U; index < (sizeof(keys) / sizeof(keys[0])); ++index)
    {
        atlas_gnss_put_u32(&payload[4U + (4U * index)], keys[index]);
    }

    gnss->config_poll_received = false;
    gnss->config_poll_was_nak = false;
    gnss->config_response_length = 0U;
    gnss->config_poll_pending = true;
    status = AtlasGnss_SendUbx(gnss,
                               UBX_CLASS_CFG,
                               UBX_ID_CFG_VALGET,
                               payload,
                               sizeof(payload),
                               false,
                               0U);
    if (status != ATLAS_OK)
    {
        gnss->config_poll_pending = false;
        return status;
    }

    started_ms = HAL_GetTick();
    while (!gnss->config_poll_received &&
           ((HAL_GetTick() - started_ms) < UBX_DEFAULT_TIMEOUT_MS))
    {
        status = AtlasGnss_Service(gnss, 512U);
        if (status != ATLAS_OK)
        {
            gnss->config_poll_pending = false;
            return status;
        }
        AtlasTime_DelayMs(1U);
    }
    gnss->config_poll_pending = false;
    if (!gnss->config_poll_received)
    {
        ++gnss->health.command_timeouts;
        return ATLAS_ERROR_TIMEOUT;
    }
    if (gnss->config_poll_was_nak)
    {
        return ATLAS_ERROR_NACK;
    }
    status = atlas_gnss_validate_ram_configuration(gnss,
                                                    measurement_period_ms,
                                                    disable_nmea);
    if (status == ATLAS_OK)
    {
        ++gnss->health.configuration_readbacks;
    }
    else
    {
        ++gnss->health.configuration_mismatches;
    }
    return status;
}

/**
 * @brief Start the NEO-M9N transport/PPS capture and prove UBX communication.
 * @param gnss Destination driver instance.
 * @param transport Initialized transport object to bind to USART1.
 * @param uart Initialized USART1 at the receiver's current baud.
 * @param pps_timer TIM2 configured for 1 MHz input capture channel 1.
 * @return ATLAS_OK or a typed transport, timeout, or identity failure.
 */
AtlasStatus AtlasGnss_Init(AtlasGnss *gnss,
                           AtlasUartTransport *transport,
                           UART_HandleTypeDef *uart,
                           TIM_HandleTypeDef *pps_timer)
{
    AtlasStatus status;
    uint32_t started_ms;

    if ((gnss == NULL) || (transport == NULL) || (uart == NULL) ||
        (pps_timer == NULL))
    {
        return ATLAS_ERROR_NULL;
    }

    memset(gnss, 0, sizeof(*gnss));
    gnss->transport = transport;
    gnss->pps_timer = pps_timer;
    gnss->parser_state = ATLAS_GNSS_PARSE_SYNC1;

    status = AtlasUartTransport_Init(transport, uart);
    if (status == ATLAS_OK)
    {
        status = AtlasUartTransport_Start(transport);
    }
    if (status != ATLAS_OK)
    {
        return status;
    }
    status = AtlasTime_StartCounter(pps_timer);
    if (status != ATLAS_OK)
    {
        return status;
    }
    if (HAL_TIM_IC_Start_IT(pps_timer, TIM_CHANNEL_1) != HAL_OK)
    {
        return ATLAS_ERROR_IO;
    }

    /* MON-VER is a side-effect-free poll and proves bidirectional UBX framing. */
    status = AtlasGnss_SendUbx(gnss,
                               UBX_CLASS_MON,
                               UBX_ID_MON_VER,
                               NULL,
                               0U,
                               false,
                               0U);
    if (status != ATLAS_OK)
    {
        return status;
    }
    started_ms = HAL_GetTick();
    while (!gnss->version_received &&
           ((HAL_GetTick() - started_ms) < UBX_DEFAULT_TIMEOUT_MS))
    {
        status = AtlasGnss_Service(gnss, 512U);
        if (status != ATLAS_OK)
        {
            return status;
        }
        AtlasTime_DelayMs(1U);
    }
    if (!gnss->version_received)
    {
        ++gnss->health.command_timeouts;
        return ATLAS_ERROR_IDENTITY;
    }

    gnss->initialized = true;
    return ATLAS_OK;
}

/**
 * @brief Parse a bounded snapshot of received bytes and dispatch complete UBX frames.
 * @param gnss Initialized or initializing GNSS instance.
 * @param byte_budget Maximum bytes to consume; zero selects 512 bytes.
 * @return ATLAS_OK or a transport/readiness failure.
 */
AtlasStatus AtlasGnss_Service(AtlasGnss *gnss, size_t byte_budget)
{
    uint8_t value;
    size_t consumed = 0U;
    AtlasStatus status;

    if ((gnss == NULL) || (gnss->transport == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    status = AtlasUartTransport_Service(gnss->transport);
    if (status != ATLAS_OK)
    {
        return status;
    }
    if (byte_budget == 0U)
    {
        byte_budget = 512U;
    }
    (void)atlas_gnss_check_continuity(gnss, HAL_GetTick());
    while ((consumed < byte_budget) &&
           AtlasUartTransport_ReadByte(gnss->transport, &value))
    {
        if (atlas_gnss_check_continuity(gnss, HAL_GetTick()))
        {
            break; /* A loss observed during this bounded drain invalidates it. */
        }
        atlas_gnss_parse_byte(gnss, value);
        gnss->last_byte_ms = HAL_GetTick();
        ++consumed;
    }
    return ATLAS_OK;
}

/**
 * @brief Send a validated UBX frame and optionally wait for its matching ACK.
 * @param gnss Initialized or initializing GNSS instance.
 * @param message_class UBX message class.
 * @param message_id UBX message identifier.
 * @param payload Payload bytes.
 * @param payload_length Payload length.
 * @param wait_for_ack true to correlate ACK/NAK.
 * @param timeout_ms Nonzero ACK timeout.
 * @return ATLAS_OK or a typed parameter, transport, NAK, or timeout result.
 */
AtlasStatus AtlasGnss_SendUbx(AtlasGnss *gnss,
                              uint8_t message_class,
                              uint8_t message_id,
                              const uint8_t *payload,
                              uint16_t payload_length,
                              bool wait_for_ack,
                              uint32_t timeout_ms)
{
    uint8_t frame[ATLAS_GNSS_MAX_UBX_PAYLOAD + UBX_FRAME_OVERHEAD];
    uint8_t checksum_a = 0U;
    uint8_t checksum_b = 0U;
    size_t index;
    AtlasStatus status;
    uint32_t started_ms;

    if ((gnss == NULL) || (gnss->transport == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if ((payload_length > ATLAS_GNSS_MAX_UBX_PAYLOAD) ||
        ((payload_length > 0U) && (payload == NULL)) ||
        (wait_for_ack && (timeout_ms == 0U)))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    frame[0] = UBX_SYNC_1;
    frame[1] = UBX_SYNC_2;
    frame[2] = message_class;
    frame[3] = message_id;
    frame[4] = (uint8_t)payload_length;
    frame[5] = (uint8_t)(payload_length >> 8);
    if (payload_length > 0U)
    {
        memcpy(&frame[6], payload, payload_length);
    }
    for (index = 2U; index < (size_t)(6U + payload_length); ++index)
    {
        checksum_a = (uint8_t)(checksum_a + frame[index]);
        checksum_b = (uint8_t)(checksum_b + checksum_a);
    }
    frame[6U + payload_length] = checksum_a;
    frame[7U + payload_length] = checksum_b;

    if (wait_for_ack)
    {
        gnss->ack_class = message_class;
        gnss->ack_id = message_id;
        gnss->ack_received = false;
        gnss->ack_was_nak = false;
        gnss->ack_pending = true;
    }
    status = AtlasUartTransport_Write(gnss->transport,
                                      frame,
                                      (size_t)payload_length + UBX_FRAME_OVERHEAD,
                                      100U);
    if ((status != ATLAS_OK) || !wait_for_ack)
    {
        gnss->ack_pending = false;
        return status;
    }

    started_ms = HAL_GetTick();
    while (!gnss->ack_received && ((HAL_GetTick() - started_ms) < timeout_ms))
    {
        status = AtlasGnss_Service(gnss, 512U);
        if (status != ATLAS_OK)
        {
            gnss->ack_pending = false;
            return status;
        }
        AtlasTime_DelayMs(1U);
    }
    gnss->ack_pending = false;
    if (!gnss->ack_received)
    {
        ++gnss->health.command_timeouts;
        return ATLAS_ERROR_TIMEOUT;
    }
    return gnss->ack_was_nak ? ATLAS_ERROR_NACK : ATLAS_OK;
}

/**
 * @brief Configure measurement rate, UBX output, and NAV-PVT in volatile RAM only.
 * @param gnss Initialized GNSS instance.
 * @param measurement_period_ms Period from 100 through 1000 milliseconds.
 * @param disable_nmea true to disable NMEA output after UBX is enabled.
 * @return ATLAS_OK only after a matching UBX ACK and exact RAM-layer readback.
 */
AtlasStatus AtlasGnss_ConfigureRam(AtlasGnss *gnss,
                                  uint16_t measurement_period_ms,
                                  bool disable_nmea)
{
    uint8_t payload[40] = {0U};
    size_t index = 4U;

    if (gnss == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!gnss->initialized)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if ((measurement_period_ms < 100U) || (measurement_period_ms > 1000U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    payload[0] = 0U; /* CFG-VALSET version 0. */
    payload[1] = UBX_CFG_LAYER_RAM;
    atlas_gnss_append_u2(payload, &index, UBX_KEY_RATE_MEAS, measurement_period_ms);
    atlas_gnss_append_u2(payload, &index, UBX_KEY_RATE_NAV, 1U);
    atlas_gnss_append_u1(payload, &index, UBX_KEY_RATE_TIMEREF, 0U); /* UTC */
    atlas_gnss_append_u1(payload, &index, UBX_KEY_UART1OUTPROT_UBX, 1U);
    atlas_gnss_append_u1(payload, &index, UBX_KEY_UART1OUTPROT_NMEA,
                         disable_nmea ? 0U : 1U);
    atlas_gnss_append_u1(payload, &index, UBX_KEY_MSGOUT_NAV_PVT_UART1, 1U);

    {
        AtlasStatus status = AtlasGnss_SendUbx(gnss,
                                               UBX_CLASS_CFG,
                                               UBX_ID_CFG_VALSET,
                                               payload,
                                               (uint16_t)index,
                                               true,
                                               UBX_DEFAULT_TIMEOUT_MS);
        if (status == ATLAS_OK)
        {
            status = atlas_gnss_readback_ram_configuration(gnss,
                                                            measurement_period_ms,
                                                            disable_nmea);
        }
        return status;
    }
}

/**
 * @brief Copy the newest NAV-PVT solution without exposing mutable driver storage.
 * @param gnss Initialized GNSS instance.
 * @param solution Destination navigation solution.
 * @param consume true to clear the new-solution flag.
 * @return true when a solution has been received.
 * @note Foreground-only accessor paired with the foreground parser/service routine.
 */
bool AtlasGnss_GetLatestNavPvt(AtlasGnss *gnss,
                               AtlasGnssNavPvt *solution,
                               bool consume)
{
    if ((gnss == NULL) || (solution == NULL) || !gnss->nav_available)
    {
        return false;
    }

    *solution = gnss->latest_nav;
    if (consume)
    {
        gnss->nav_available = false;
    }
    return true;
}

/**
 * @brief Copy a coherent snapshot of the ISR-owned PPS capture state.
 * @param gnss GNSS driver instance.
 * @param pps Destination snapshot.
 * @return true after a coherent pulse snapshot; false for no pulse, NULL, or
 *         repeated ISR contention during the bounded copy attempts.
 */
bool AtlasGnss_GetPps(const AtlasGnss *gnss, AtlasGnssPps *pps)
{
    uint32_t pulse_count_before;
    uint32_t pulse_count_after;
    uint32_t attempt;

    if ((gnss == NULL) || (pps == NULL))
    {
        return false;
    }
    for (attempt = 0U; attempt < UBX_PPS_SNAPSHOT_ATTEMPTS; ++attempt)
    {
        pulse_count_before = gnss->pps.pulse_count;
        __DMB();
        pps->latest_capture_us = gnss->pps.latest_capture_us;
        pps->period_us = gnss->pps.period_us;
        pps->valid_period = gnss->pps.valid_period;
        __DMB();
        pulse_count_after = gnss->pps.pulse_count;
        if (pulse_count_before == pulse_count_after)
        {
            pps->pulse_count = pulse_count_after;
            return pulse_count_after != 0U;
        }
    }
    /* Fail closed instead of permitting interrupt activity to create an
       unbounded task-level retry loop; the next I/O cycle retries naturally. */
    return false;
}

/**
 * @brief Record one TIM2 channel-1 GNSS PPS capture in ISR context.
 * @param gnss Driver instance; NULL is ignored.
 */
void AtlasGnss_OnPpsCapture(AtlasGnss *gnss)
{
    uint32_t captured;
    uint32_t previous;

    if ((gnss == NULL) || (gnss->pps_timer == NULL))
    {
        return;
    }
    captured = HAL_TIM_ReadCapturedValue(gnss->pps_timer, TIM_CHANNEL_1);
    previous = gnss->pps.latest_capture_us;
    gnss->pps.latest_capture_us = captured;
    if (gnss->pps.pulse_count > 0U)
    {
        /* Unsigned subtraction remains valid when the 32-bit timer wraps. */
        gnss->pps.period_us = captured - previous;
        gnss->pps.valid_period = true;
    }
    /* Publish the fields above before pulse_count changes for snapshot readers. */
    __DMB();
    ++gnss->pps.pulse_count;
}
