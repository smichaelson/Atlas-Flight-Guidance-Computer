/**
 * @file test_gnss_integration.c
 * @brief State-aware GNSS startup and malformed-stream acceptance probes.
 *
 * Major functions:
 * - HAL_TIM_Base_Start(): models the bundled HAL's READY-to-BUSY contract.
 * - review_feed()/review_reply(): supply real checksum-framed UART input.
 * - main(): checks stale-RX recovery, explicit retry, shared timer use, and resync.
 *
 * Links the production GNSS, UART, status, and time implementations. The timer
 * precondition matches AtlasBno085_Init() preceding AtlasGnss_Init() in board
 * startup; this does not simulate BNO085 hardware or the complete board boot.
 */
#include "atlas_gnss.h"
#include "atlas_time.h"
#include <stdio.h>
#include <string.h>

/* Reuse the existing transport mocks but replace their always-OK timer start. */
#define HAL_TIM_Base_Start AtlasReview_UnusedAlwaysOkTimerStart
#include "../host/test_hal_stubs.c"
#undef HAL_TIM_Base_Start

static TIM_HandleTypeDef *review_started_timer;
static AtlasUartTransport *review_transport;
static unsigned review_polls;
static unsigned review_timer_starts;

/**
 * @brief Model HAL_TIM_Base_Start's rejection of a timer already in BUSY state.
 * @param timer Mock timer whose first successful start is tracked.
 * @return HAL_OK once, HAL_ERROR on a duplicate start of the same handle.
 */
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer)
{
    ++review_timer_starts;
    if (review_started_timer == timer) { return HAL_ERROR; }
    if (timer == NULL || timer->Instance == NULL ||
        timer->State != HAL_TIM_STATE_READY) { return HAL_ERROR; }
    review_started_timer = timer;
    timer->State = HAL_TIM_STATE_BUSY;
    timer->Instance->CR1 |= TIM_CR1_CEN;
    return HAL_OK;
}

/**
 * @brief Deliver bytes through the production receive callback in bounded chunks.
 * @param bytes Input stream.
 * @param length Number of bytes.
 */
static void review_feed(const uint8_t *bytes, size_t length)
{
    while (length != 0U)
    {
        const size_t chunk = length > ATLAS_UART_RX_CHUNK_CAPACITY ?
                             ATLAS_UART_RX_CHUNK_CAPACITY : length;
        memcpy(review_transport->rx_chunk, bytes, chunk);
        HAL_UARTEx_RxEventCallback(review_transport->uart, (uint16_t)chunk);
        bytes += chunk;
        length -= chunk;
    }
}

/**
 * @brief Frame a short valid UBX message, including the two checksum bytes.
 * @param frame Destination with 100-byte capacity.
 * @param cls Message class.
 * @param id Message identifier.
 * @param payload Payload bytes.
 * @param length Payload length, at most 92 bytes in this probe.
 */
static void review_build(uint8_t frame[100], uint8_t cls, uint8_t id, const uint8_t *payload, size_t length)
{
    frame[0] = 0xB5U; frame[1] = 0x62U; frame[2] = cls; frame[3] = id;
    frame[4] = (uint8_t)length; frame[5] = 0U;
    uint8_t a = 0U, b = 0U;
    memcpy(&frame[6], payload, length);
    for (size_t i = 2U; i < length + 6U; ++i)
    {
        a = (uint8_t)(a + frame[i]);
        b = (uint8_t)(b + a);
    }
    frame[length + 6U] = a;
    frame[length + 7U] = b;
}
/** @brief Build and feed one short valid UBX frame. @param cls Class. @param id ID.
 * @param payload Bytes. @param length Payload length <=92. */
static void review_frame(uint8_t cls, uint8_t id, const uint8_t *payload, size_t length)
{
    uint8_t frame[100];
    review_build(frame, cls, id, payload, length);
    review_feed(frame, length + 8U);
}

/** @brief Exercise parser age, fragmentation, corruption and transport resync.
 * @param gnss Initialized real parser. @return Number of failed expectations. */
static unsigned review_parser_cases(AtlasGnss *gnss)
{
    unsigned failures = 0U;
    uint8_t nav[92] = {0}, frame[100];
    const uint32_t before = gnss->health.nav_pvt_frames;
    nav[48] = 0xB5U; nav[49] = 0x62U; /* Payload sync bytes are data, not resync. */
    review_build(frame, 1U, 7U, nav, sizeof(nav));
    for (size_t i = 0; i < sizeof(frame); ++i)
    {
        review_feed(frame + i, 1U);
        (void)AtlasGnss_Service(gnss, 1U);
        HAL_Delay(2U);
    }
    if (gnss->health.nav_pvt_frames != before + 1U) ++failures;
    frame[99] ^= 0x55U;
    review_feed(frame, sizeof(frame));
    (void)AtlasGnss_Service(gnss, 0U);
    if (gnss->health.nav_pvt_frames != before + 1U || gnss->health.checksum_errors == 0U) ++failures;
    frame[99] ^= 0x55U;

    /* A truncated IN-RANGE length must time out too, including across tick wrap. */
    atlas_test_tick = UINT32_MAX - 100U;
    const uint32_t old_timeouts = gnss->health.frame_timeouts;
    review_feed(frame, 11U);
    (void)AtlasGnss_Service(gnss, 0U);
    HAL_Delay(251U);
    review_feed(frame, sizeof(frame));
    (void)AtlasGnss_Service(gnss, 0U);
    if (gnss->health.nav_pvt_frames != before + 2U || gnss->health.frame_timeouts <= old_timeouts) ++failures;

    /* Whole-frame age wins even when every inter-byte gap is under 250 ms. */
    const uint8_t long_header[] = {0xB5,0x62,0x01,0x07,0x00,0x02};
    review_feed(long_header, sizeof(long_header));
    (void)AtlasGnss_Service(gnss, 0U);
    for (unsigned i = 0; i < 11U; ++i)
    {
        HAL_Delay(200U);
        const uint8_t zero = 0;
        review_feed(&zero, 1U);
        (void)AtlasGnss_Service(gnss, 0U);
    }
    review_feed(frame, sizeof(frame));
    (void)AtlasGnss_Service(gnss, 0U);
    if (gnss->health.nav_pvt_frames != before + 3U) ++failures;

    /* Ring overflow and receiver restart break frame continuity; discard backlog. */
    const uint32_t old_resync = gnss->health.transport_resynchronizations;
    uint8_t noise[64]; memset(noise, 0x19, sizeof(noise));
    for (unsigned i = 0; i < 20U; ++i) review_feed(noise, sizeof(noise));
    (void)AtlasGnss_Service(gnss, 0U);
    review_feed(frame, sizeof(frame));
    (void)AtlasGnss_Service(gnss, 0U);
    ++review_transport->health.receive_restarts;
    review_feed(frame, 10U);
    (void)AtlasGnss_Service(gnss, 0U);
    review_feed(frame, sizeof(frame));
    (void)AtlasGnss_Service(gnss, 0U);
    if (gnss->health.nav_pvt_frames != before + 5U ||
        gnss->health.transport_resynchronizations < old_resync + 2U) ++failures;
    printf("%s R08: fragmentation, embedded sync, CRC, bounded frame age, wrap, overflow and restart\n",
           failures == 0U ? "PASS" : "FAIL");
    return failures;
}

/**
 * @brief Supply a valid MON-VER response only when the real driver polls it.
 * @param uart Transmitting mock UART.
 * @param data Command bytes.
 * @param length Command length.
 */
static void review_reply(UART_HandleTypeDef *uart, const uint8_t *data, uint16_t length)
{
    uint8_t payload[40] = {0};
    (void)uart;
    if (length == 8U && data[2] == 0x0AU && data[3] == 0x04U)
    {
        ++review_polls;
        memcpy(payload, "REVIEW-SW", 9U);
        memcpy(&payload[30], "REVIEW-HW", 9U);
        review_frame(0x0AU, 0x04U, payload, sizeof(payload));
    }
}

/** @brief Run clean controls and failure-path contracts. @return Acceptance result. */
int main(void)
{
    static AtlasGnss gnss;
    static AtlasUartTransport transport;
    static UART_HandleTypeDef uart;
    static TIM_HandleTypeDef timer;
    static TIM_TypeDef counter_registers;
    uint8_t nav[92] = {0};
    const uint8_t truncated_header[] = {0xB5U, 0x62U, 0x01U, 0x07U, 0xFFU, 0xFFU};
    unsigned failures = 0U;
    uart.Instance = &uart;
    timer.Instance = &counter_registers;
    timer.State = HAL_TIM_STATE_READY;
    review_transport = &transport;

    /* Model the field failure: NMEA arrived before staged RX was armed and the
     * first identity attempt receives no MON-VER response. PPS must stay idle so
     * an operator-requested retry can safely begin from a quiesced transport. */
    AtlasTest_ResetUartReceiveTrace();
    AtlasTest_SetUartStaleReceive(true);
    AtlasTest_SetUartTransmitHook(NULL);
    AtlasStatus status = AtlasGnss_Init(&gnss, &transport, &uart, &timer);
    if (status != ATLAS_ERROR_IDENTITY ||
        gnss.health.last_failure_stage != ATLAS_GNSS_FAILURE_MON_VER_TIMEOUT ||
        gnss.health.pps_capture_started || review_timer_starts != 0U ||
        AtlasTest_GetUartAbortCount() != 1U || AtlasTest_GetUartArmCount() != 1U ||
        transport.health.bytes_transmitted != 96U ||
        gnss.health.command_timeouts != 1U)
    {
        puts("FAIL GNSS retry: first identity failure did not remain retry-safe");
        return 2;
    }
    if (AtlasUartTransport_Stop(&transport) != ATLAS_OK)
    {
        puts("HARNESS ERROR: could not quiesce failed GNSS transport");
        return 2;
    }
    AtlasTest_SetUartTransmitHook(review_reply);
    status = AtlasGnss_Init(&gnss, &transport, &uart, &timer);
    if (status != ATLAS_OK || review_polls != 1U)
    {
        puts("FAIL GNSS retry: explicit second identity attempt did not recover");
        return 2;
    }
    if (gnss.health.last_failure_stage != ATLAS_GNSS_FAILURE_NONE ||
        gnss.health.last_failure_status != ATLAS_OK ||
        !gnss.health.pps_capture_started)
    {
        puts("FAIL GNSS retry: successful attempt retained invalid diagnostics");
        return 2;
    }
    puts("PASS GNSS: stale pre-probe RX flushed; failed identity remained manually retryable");
    review_frame(0x01U, 0x07U, nav, sizeof(nav));
    if (AtlasGnss_Service(&gnss, 0U) != ATLAS_OK || gnss.health.nav_pvt_frames != 1U)
    {
        puts("HARNESS ERROR: clean NAV-PVT control failed");
        return 2;
    }
    puts("CONTROL PASS: isolated GNSS startup and clean NAV-PVT");

    review_feed(truncated_header, sizeof(truncated_header));
    (void)AtlasGnss_Service(&gnss, 0U);
    HAL_Delay(5000U); /* A later intact frame should not remain trapped in a dead frame. */
    review_frame(0x01U, 0x07U, nav, sizeof(nav));
    (void)AtlasGnss_Service(&gnss, 0U);
    const int resync_failed = gnss.health.nav_pvt_frames != 2U;
    printf("%s R08: after 5 s and a valid NAV-PVT, discard_remaining=%lu\n",
           resync_failed ? "FAIL" : "PASS", (unsigned long)gnss.discard_remaining);
    failures += resync_failed ? 1U : 0U;
    failures += review_parser_cases(&gnss);

    /* Independent board-order fixture: the BNO085 clock request precedes the
     * first GNSS capture request. The sole shared-counter helper owns HAL start. */
    (void)AtlasUartTransport_Stop(&transport);
    counter_registers.CR1 = 0U;
    timer.State = HAL_TIM_STATE_READY;
    timer.counter = 123456U;
    review_started_timer = NULL;
    review_timer_starts = 0U;
    if (AtlasTime_StartCounter(&timer) != ATLAS_OK) { return 2; }
    review_polls = 0U;
    status = AtlasGnss_Init(&gnss, &transport, &uart, &timer);
    const int startup_failed = status != ATLAS_OK || review_polls != 1U ||
                               review_timer_starts != 1U || timer.counter != 123456U;
    printf("%s R01: GNSS with shared running timer returned %s; MON-VER polls=%u\n",
           startup_failed ? "FAIL" : "PASS", AtlasStatus_Name(status), review_polls);
    failures += startup_failed ? 1U : 0U;

    /* A BUSY label alone must not mask a stopped counter. */
    counter_registers.CR1 = 0U;
    if (AtlasTime_StartCounter(&timer) != ATLAS_ERROR_STATE)
    {
        puts("FAIL R01: stopped/BUSY timer incorrectly accepted");
        ++failures;
    }
    return failures != 0U ? 1 : 0;
}
