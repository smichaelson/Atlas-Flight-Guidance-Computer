/**
 * @file atlas_gnss.h
 * @brief u-blox NEO-M9N UBX transport, configuration, NAV-PVT, and PPS firmware.
 *
 * Major functions:
 * - AtlasGnss_Init(): starts UART reception and proves identity with UBX-MON-VER.
 * - AtlasGnss_ConfigureRam(): configures rate and UART1 messages without writing flash.
 * - AtlasGnss_Service(): validates UBX frames and decodes UBX-NAV-PVT.
 * - AtlasGnssFailureStage: identifies the exact startup/configuration phase that failed.
 * - AtlasGnss_GetPps(): snapshots ISR-owned timing state coherently.
 * - AtlasGnss_OnPpsCapture(): timestamps the GNSS time-pulse edge in ISR context.
 */

#ifndef ATLAS_GNSS_H
#define ATLAS_GNSS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"
#include "atlas_uart_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_GNSS_MAX_UBX_PAYLOAD (512U)
#define ATLAS_GNSS_VERSION_TEXT_CAPACITY (31U)
#define ATLAS_GNSS_CONFIG_RESPONSE_CAPACITY (64U)

/** @brief Fully decoded UBX-NAV-PVT navigation solution. */
typedef struct
{
    uint32_t time_of_week_ms;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t valid_flags;
    uint32_t time_accuracy_ns;
    int32_t nanoseconds;
    uint8_t fix_type;
    uint8_t flags;
    uint8_t flags2;
    uint8_t satellites_used;
    int32_t longitude_1e7_deg;
    int32_t latitude_1e7_deg;
    int32_t height_ellipsoid_mm;
    int32_t height_msl_mm;
    uint32_t horizontal_accuracy_mm;
    uint32_t vertical_accuracy_mm;
    int32_t velocity_north_mm_s;
    int32_t velocity_east_mm_s;
    int32_t velocity_down_mm_s;
    int32_t ground_speed_mm_s;
    int32_t heading_motion_1e5_deg;
    uint32_t speed_accuracy_mm_s;
    uint32_t heading_accuracy_1e5_deg;
    uint16_t position_dop_0p01;
    uint8_t flags3;
    int32_t heading_vehicle_1e5_deg;
    int16_t magnetic_declination_1e2_deg;
    uint16_t magnetic_accuracy_1e2_deg;
    uint32_t received_at_ms;
} AtlasGnssNavPvt;

/** @brief Captured GNSS time-pulse state. */
typedef struct
{
    volatile uint32_t latest_capture_us;
    volatile uint32_t period_us;
    volatile uint32_t pulse_count;
    volatile bool valid_period;
} AtlasGnssPps;

/** @brief Bounded GNSS startup/configuration stage retained after a failure. */
typedef enum
{
    ATLAS_GNSS_FAILURE_NONE = 0,
    ATLAS_GNSS_FAILURE_TRANSPORT_INIT,
    ATLAS_GNSS_FAILURE_TRANSPORT_START,
    ATLAS_GNSS_FAILURE_MON_VER_WRITE,
    ATLAS_GNSS_FAILURE_MON_VER_SERVICE,
    ATLAS_GNSS_FAILURE_MON_VER_TIMEOUT,
    ATLAS_GNSS_FAILURE_TIMEBASE_START,
    ATLAS_GNSS_FAILURE_PPS_CAPTURE_START,
    ATLAS_GNSS_FAILURE_CONFIGURATION_WRITE,
    ATLAS_GNSS_FAILURE_CONFIGURATION_READBACK,
    ATLAS_GNSS_FAILURE_RUNTIME_SERVICE
} AtlasGnssFailureStage;

/** @brief GNSS parser and transport diagnostics. */
typedef struct
{
    uint32_t bytes_parsed;
    uint32_t valid_ubx_frames;
    uint32_t checksum_errors;
    uint32_t oversize_frames;
    uint32_t frame_timeouts;
    uint32_t transport_resynchronizations;
    uint32_t nav_pvt_frames;
    uint32_t malformed_nav_pvt;
    uint32_t acknowledgements;
    uint32_t negative_acknowledgements;
    uint32_t command_timeouts;
    uint32_t configuration_readbacks;
    uint32_t configuration_mismatches;
    uint32_t last_uart_error;
    AtlasGnssFailureStage last_failure_stage;
    AtlasStatus last_failure_status;
    bool pps_capture_started;
} AtlasGnssHealth;

/** @brief Internal UBX byte parser state. */
typedef enum
{
    ATLAS_GNSS_PARSE_SYNC1 = 0,
    ATLAS_GNSS_PARSE_SYNC2,
    ATLAS_GNSS_PARSE_CLASS,
    ATLAS_GNSS_PARSE_ID,
    ATLAS_GNSS_PARSE_LENGTH1,
    ATLAS_GNSS_PARSE_LENGTH2,
    ATLAS_GNSS_PARSE_PAYLOAD,
    ATLAS_GNSS_PARSE_CHECKSUM_A,
    ATLAS_GNSS_PARSE_CHECKSUM_B,
    ATLAS_GNSS_PARSE_DISCARD
} AtlasGnssParserState;

/** @brief GNSS driver instance. */
typedef struct
{
    AtlasUartTransport *transport;
    TIM_HandleTypeDef *pps_timer;
    AtlasGnssParserState parser_state;
    uint8_t message_class;
    uint8_t message_id;
    uint16_t payload_length;
    uint16_t payload_index;
    uint32_t discard_remaining;
    uint32_t frame_started_ms;
    uint32_t last_byte_ms;
    uint32_t observed_dropped_bytes;
    uint32_t observed_receive_restarts;
    uint8_t checksum_a;
    uint8_t checksum_b;
    uint8_t received_checksum_a;
    uint8_t payload[ATLAS_GNSS_MAX_UBX_PAYLOAD];
    volatile bool ack_pending;
    volatile bool ack_received;
    volatile bool ack_was_nak;
    volatile uint8_t ack_class;
    volatile uint8_t ack_id;
    volatile bool version_received;
    volatile bool config_poll_pending;
    volatile bool config_poll_received;
    volatile bool config_poll_was_nak;
    uint16_t config_response_length;
    uint8_t config_response[ATLAS_GNSS_CONFIG_RESPONSE_CAPACITY];
    char software_version[ATLAS_GNSS_VERSION_TEXT_CAPACITY];
    char hardware_version[ATLAS_GNSS_VERSION_TEXT_CAPACITY];
    AtlasGnssNavPvt latest_nav;
    volatile bool nav_available;
    AtlasGnssPps pps;
    bool initialized;
    AtlasGnssHealth health;
} AtlasGnss;

/**
 * @brief Clear stale USART RX state, prove NEO-M9N UBX identity, then start PPS capture.
 * @param gnss Destination driver instance.
 * @param transport Initialized transport object to bind to USART1.
 * @param uart Initialized USART1 at the receiver's current baud (factory 38400).
 * @param pps_timer TIM2 configured for 1 MHz input capture channel 1.
 * @return ATLAS_OK or a typed transport, timer, or identity failure.
 * @note The receive preflight is required for staged bring-up because the receiver
 *       emits NMEA before this function is called and can otherwise leave USART1
 *       with a pending overrun that makes STM32 ReceiveToIdle arming fail.
 */
AtlasStatus AtlasGnss_Init(AtlasGnss *gnss,
                           AtlasUartTransport *transport,
                           UART_HandleTypeDef *uart,
                           TIM_HandleTypeDef *pps_timer);

/**
 * @brief Parse a bounded snapshot of received bytes and dispatch complete UBX frames.
 * @param gnss Initialized GNSS instance.
 * @param byte_budget Maximum bytes to consume; zero selects 512 bytes.
 * @return ATLAS_OK or a transport/readiness failure.
 */
AtlasStatus AtlasGnss_Service(AtlasGnss *gnss, size_t byte_budget);

/**
 * @brief Configure measurement rate, UBX output, and NAV-PVT in volatile RAM only.
 * @param gnss Initialized GNSS instance.
 * @param measurement_period_ms Period from 100 through 1000 milliseconds.
 * @param disable_nmea true to disable NMEA output on UART1 after UBX is enabled.
 * @return ATLAS_OK only after a matching UBX-ACK-ACK and exact RAM-layer
 *         UBX-CFG-VALGET readback are received.
 * @note No BBR or flash layer is modified by this function.
 */
AtlasStatus AtlasGnss_ConfigureRam(AtlasGnss *gnss,
                                  uint16_t measurement_period_ms,
                                  bool disable_nmea);

/**
 * @brief Send a validated UBX frame and optionally wait for its matching ACK.
 * @param gnss Initialized GNSS instance.
 * @param message_class UBX message class.
 * @param message_id UBX message identifier.
 * @param payload Payload bytes; may be NULL only when payload_length is zero.
 * @param payload_length Payload length no greater than ATLAS_GNSS_MAX_UBX_PAYLOAD.
 * @param wait_for_ack true for commands that generate UBX-ACK-ACK/NAK.
 * @param timeout_ms Nonzero response timeout when wait_for_ack is true.
 * @return ATLAS_OK or a typed parameter, transport, NAK, or timeout result.
 */
AtlasStatus AtlasGnss_SendUbx(AtlasGnss *gnss,
                              uint8_t message_class,
                              uint8_t message_id,
                              const uint8_t *payload,
                              uint16_t payload_length,
                              bool wait_for_ack,
                              uint32_t timeout_ms);

/**
 * @brief Copy the newest NAV-PVT solution without exposing mutable driver storage.
 * @param gnss Initialized GNSS instance.
 * @param solution Destination navigation solution.
 * @param consume true to clear the new-solution flag.
 * @return true when a solution has been received.
 * @note Call from foreground code. AtlasGnss_Service() is the only writer and is
 *       also a foreground operation, so this accessor does not alter IRQ state.
 */
bool AtlasGnss_GetLatestNavPvt(AtlasGnss *gnss,
                               AtlasGnssNavPvt *solution,
                               bool consume);

/**
 * @brief Copy a coherent snapshot of the ISR-owned PPS capture state.
 * @param gnss GNSS driver instance.
 * @param pps Destination snapshot.
 * @return true after a coherent pulse snapshot; false for no pulse, NULL, or
 *         repeated ISR contention during the bounded copy attempts.
 */
bool AtlasGnss_GetPps(const AtlasGnss *gnss, AtlasGnssPps *pps);

/**
 * @brief Record one TIM2 channel-1 GNSS PPS capture in ISR context.
 * @param gnss Driver instance; NULL is ignored.
 * @note Call from HAL_TIM_IC_CaptureCallback() when htim->Channel is active CH1.
 */
void AtlasGnss_OnPpsCapture(AtlasGnss *gnss);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_GNSS_H */
