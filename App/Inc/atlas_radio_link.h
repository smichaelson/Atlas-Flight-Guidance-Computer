/**
 * @file atlas_radio_link.h
 * @brief Bounded bench-only radio challenge/reply protocol; never carries output commands.
 * Major functions: Init binds a board UID, Test starts one round trip, Feed parses
 * fragmented bytes, Service advances without waiting for a peer, Snapshot reports freshness.
 */
#ifndef ATLAS_RADIO_LINK_H
#define ATLAS_RADIO_LINK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "atlas_status.h"

#define ATLAS_RADIO_FRAME_SIZE 44U
#define ATLAS_RADIO_TIMEOUT_MS 2000U
#define ATLAS_RADIO_INTERVAL_MS 3000U
#define ATLAS_RADIO_FRESH_MS 6000U

/** @brief Last explicit test outcome, separate from background monitoring. */
typedef enum {
    ATLAS_RADIO_TEST_IDLE, ATLAS_RADIO_TEST_WAITING, ATLAS_RADIO_TEST_ACK,
    ATLAS_RADIO_TEST_TIMEOUT, ATLAS_RADIO_TEST_TX_ERROR, ATLAS_RADIO_TEST_CANCELLED
} AtlasRadioTest;

/** @brief Copyable telemetry. UINT32_MAX ack_age_ms means no acknowledged request. */
typedef struct {
    uint32_t monitoring, connected, waiting, peer[3], ack_age_ms, rtt_ms;
    uint32_t sent, received, replies, acknowledgements, timeouts, tx_errors, invalid;
    uint32_t test, test_sequence, test_rtt_ms, test_peer[3];
} AtlasRadioLinkSnapshot;

/** @brief Sole-owner parser and transaction state; no heap, RTOS, or HAL dependency. */
typedef struct {
    AtlasRadioLinkSnapshot state;
    uint32_t uid[3], sequence, challenge, sent_ms, probe_ms, ack_ms, reply_ms;
    bool have_ack, have_reply, manual, reply_pending;
    uint8_t rx[ATLAS_RADIO_FRAME_SIZE], reply[ATLAS_RADIO_FRAME_SIZE];
    size_t used;
} AtlasRadioLink;

/** @brief Bounded UART send callback; a successful write is not proof of RF delivery. */
typedef AtlasStatus (*AtlasRadioSend)(void *context, const uint8_t *bytes, size_t length);

/** @brief Initialize passive responder only. @param link State. @param uid Full MCU UID. */
void AtlasRadioLink_Init(AtlasRadioLink *link, const uint32_t uid[3]);
/** @brief Start an explicit test, superseding a background challenge.
 * @param link State. @param now Monotonic milliseconds. @param monitor Keep testing every 3 s.
 * @param send Bounded UART callback. @param context Callback argument. @return UART/start result. */
AtlasStatus AtlasRadioLink_Test(AtlasRadioLink *link, uint32_t now, bool monitor,
                               AtlasRadioSend send, void *context);
/** @brief Stop own requests and clear proof of connection; passive replies remain enabled. */
void AtlasRadioLink_Stop(AtlasRadioLink *link);
/** @brief Feed arbitrary/fragmented UART bytes; validates address, type and CRC before acting. */
void AtlasRadioLink_Feed(AtlasRadioLink *link, const uint8_t *bytes, size_t length, uint32_t now);
/** @brief Send at most one frame and process deadlines; never waits for a reply. */
void AtlasRadioLink_Service(AtlasRadioLink *link, uint32_t now, AtlasRadioSend send, void *context);
/** @brief Publish current freshness with wrap-safe millisecond arithmetic. */
void AtlasRadioLink_Snapshot(const AtlasRadioLink *link, uint32_t now, AtlasRadioLinkSnapshot *snapshot);
#endif
