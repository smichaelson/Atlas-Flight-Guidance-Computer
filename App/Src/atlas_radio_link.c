/**
 * @file atlas_radio_link.c
 * @brief Fixed-size, CRC-protected Atlas bench ping/ack with bounded traffic and timeouts.
 * Major functions: Feed resynchronizes on valid frames; Service sends rate-limited ACKs
 * and optional heartbeats; Test requires a matching peer response, never a local echo.
 * This detects corruption and stale/foreign replies; it is not authentication.
 */
#include "atlas_radio_link.h"
#include <string.h>

#define LINK_PING 1U
#define LINK_ACK 2U
#define LINK_REPLY_INTERVAL_MS 100U
static const uint8_t magic[4] = {'A','T','L','R'};

/** @brief Decode a little-endian integer without alignment assumptions.
 * @param p Four input bytes. @return Integer. */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}
/** @brief Encode a little-endian integer. @param p Four output bytes. @param value Integer. */
static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i=0U; i<4U; ++i) p[i]=(uint8_t)(value >> (8U*i));
}
/** @brief Standard reflected CRC-32/ISO-HDLC over header and payload.
 * @param p Input bytes. @param length Byte count. @return Final CRC. */
static uint32_t crc32(const uint8_t *p, size_t length)
{
    uint32_t crc=UINT32_MAX;
    for (size_t i=0U; i<length; ++i) {
        crc ^= p[i];
        for (unsigned bit=0U; bit<8U; ++bit)
            crc=(crc>>1U)^((crc&1U)?UINT32_C(0xedb88320):0U);
    }
    return ~crc;
}
/** @brief Compare all 96 UID bits. @param a First UID. @param b Second UID. @return Equality. */
static bool same(const uint32_t a[3], const uint32_t b[3])
{
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2];
}
/** @brief Build the only two supported packet types. Zero destination means discovery ping.
 * @param out Output frame. @param type PING or ACK. @param source Sender UID.
 * @param destination Peer UID or zero. @param sequence Challenge number. @param challenge Origin time. */
static void frame(uint8_t out[ATLAS_RADIO_FRAME_SIZE], uint8_t type,
                  const uint32_t source[3], const uint32_t destination[3],
                  uint32_t sequence, uint32_t challenge)
{
    memcpy(out,magic,sizeof(magic));
    out[4]=1U; out[5]=type; out[6]=0U; out[7]=0U;
    for (unsigned i=0U; i<3U; ++i) {
        put32(out+8U+4U*i,source[i]); put32(out+20U+4U*i,destination[i]);
    }
    put32(out+32U,sequence); put32(out+36U,challenge);
    put32(out+40U,crc32(out,40U));
}

/** @brief Initialize a passive responder. @param link State. @param uid Full MCU identity. */
void AtlasRadioLink_Init(AtlasRadioLink *link, const uint32_t uid[3])
{
    memset(link,0,sizeof(*link)); memcpy(link->uid,uid,sizeof(link->uid));
    link->state.ack_age_ms=UINT32_MAX;
}

/** @brief Start one challenge; only an addressed, matching ACK can complete it.
 * @param link State. @param now Milliseconds. @param manual Explicit user test.
 * @param send UART callback. @param context Callback argument. @return Transmit status. */
static AtlasStatus start(AtlasRadioLink *link, uint32_t now, bool manual,
                         AtlasRadioSend send, void *context)
{
    uint8_t packet[ATLAS_RADIO_FRAME_SIZE];
    const uint32_t broadcast[3]={0U,0U,0U};
    if (++link->sequence==0U) ++link->sequence;
    link->challenge=now;
    link->sent_ms=link->probe_ms=now;
    link->manual=manual;
    link->state.waiting=1U;
    if (manual) {
        link->state.test=ATLAS_RADIO_TEST_WAITING;
        link->state.test_sequence=link->sequence;
        link->state.test_rtt_ms=0U;
        memset(link->state.test_peer,0,sizeof(link->state.test_peer));
    }
    frame(packet,LINK_PING,link->uid,broadcast,link->sequence,link->challenge);
    const AtlasStatus status=send(context,packet,sizeof(packet));
    if (status==ATLAS_OK) ++link->state.sent;
    else {
        ++link->state.tx_errors;
        link->state.waiting=0U;
        link->have_ack=false;
        if (manual) link->state.test=ATLAS_RADIO_TEST_TX_ERROR;
    }
    return status;
}

/** @brief Start a numbered explicit test, replacing a background challenge.
 * @param link State. @param now Milliseconds. @param monitor Enable periodic checks.
 * @param send UART callback. @param context Callback argument. @return Start/transport status. */
AtlasStatus AtlasRadioLink_Test(AtlasRadioLink *link, uint32_t now, bool monitor,
                               AtlasRadioSend send, void *context)
{
    if (link->state.waiting && link->manual) return ATLAS_ERROR_BUSY;
    if (monitor) link->state.monitoring=1U;
    return start(link,now,true,send,context);
}

/** @brief Stop probes and invalidate proof, retaining passive reception. @param link State. */
void AtlasRadioLink_Stop(AtlasRadioLink *link)
{
    link->state.monitoring=link->state.waiting=0U;
    link->have_ack=false;
    link->reply_pending=false;
    link->used=0U;
    if (link->state.test==ATLAS_RADIO_TEST_WAITING)
        link->state.test=ATLAS_RADIO_TEST_CANCELLED;
}

/** @brief Process only a structurally valid frame; never echo arbitrary data or ACK an ACK.
 * @param link State with complete valid frame. @param now Milliseconds. */
static void accept(AtlasRadioLink *link, uint32_t now)
{
    uint32_t source[3], destination[3];
    const uint32_t zero[3]={0U,0U,0U};
    for (unsigned i=0U; i<3U; ++i) {
        source[i]=get32(link->rx+8U+4U*i); destination[i]=get32(link->rx+20U+4U*i);
    }
    if (same(source,zero) || same(source,link->uid)) return;
    const uint32_t sequence=get32(link->rx+32U), challenge=get32(link->rx+36U);
    if (sequence==0U) return;
    if (link->rx[5]==LINK_PING) {
        if (!same(destination,zero) && !same(destination,link->uid)) return;
        ++link->state.received;
        /* Drop excess requests, including duplicates, rather than building a reply backlog. */
        if (link->reply_pending || (link->have_reply &&
            (uint32_t)(now-link->reply_ms)<LINK_REPLY_INTERVAL_MS)) return;
        frame(link->reply,LINK_ACK,link->uid,source,sequence,challenge);
        link->reply_pending=true;
    } else if (same(destination,link->uid) && link->state.waiting &&
               sequence==link->sequence && challenge==link->challenge &&
               (uint32_t)(now-link->sent_ms)<ATLAS_RADIO_TIMEOUT_MS) {
        link->state.waiting=0U;
        link->have_ack=true; link->ack_ms=now;
        link->state.rtt_ms=(uint32_t)(now-link->sent_ms);
        memcpy(link->state.peer,source,sizeof(source));
        ++link->state.acknowledgements;
        if (link->manual) {
            link->state.test=ATLAS_RADIO_TEST_ACK;
            link->state.test_rtt_ms=link->state.rtt_ms;
            memcpy(link->state.test_peer,source,sizeof(source));
        }
    }
}

/** @brief Consume a bounded batch of received bytes and resynchronize after damage.
 * @param link State. @param bytes Received bytes. @param length Byte count. @param now Milliseconds. */
void AtlasRadioLink_Feed(AtlasRadioLink *link, const uint8_t *bytes, size_t length, uint32_t now)
{
    for (size_t i=0U; i<length; ++i) {
        link->rx[link->used++]=bytes[i];
        /* Sliding prefix match also recovers after truncation, corruption and inserted noise. */
        while (link->used && memcmp(link->rx,magic,link->used<4U?link->used:4U)!=0) {
            --link->used; memmove(link->rx,link->rx+1U,link->used);
        }
        if (link->used!=ATLAS_RADIO_FRAME_SIZE) continue;
        if (link->rx[4]==1U && (link->rx[5]==LINK_PING || link->rx[5]==LINK_ACK) &&
            link->rx[6]==0U && link->rx[7]==0U && get32(link->rx+40U)==crc32(link->rx,40U)) {
            accept(link,now); link->used=0U;
        } else {
            ++link->state.invalid;
            --link->used; memmove(link->rx,link->rx+1U,link->used);
            /* Next byte resumes prefix search; capacity remains bounded. */
        }
    }
}

/** @brief Advance deadlines and send at most one frame without waiting for reception.
 * @param link State. @param now Milliseconds. @param send UART callback. @param context Callback argument. */
void AtlasRadioLink_Service(AtlasRadioLink *link, uint32_t now, AtlasRadioSend send, void *context)
{
    if (link->have_ack && (uint32_t)(now-link->ack_ms)>=ATLAS_RADIO_FRESH_MS)
        link->have_ack=false;
    if (link->state.waiting && (uint32_t)(now-link->sent_ms)>=ATLAS_RADIO_TIMEOUT_MS) {
        link->state.waiting=0U;
        ++link->state.timeouts;
        link->have_ack=false;
        if (link->manual) link->state.test=ATLAS_RADIO_TEST_TIMEOUT;
    }
    if (link->reply_pending) {
        link->reply_pending=false;
        link->have_reply=true; link->reply_ms=now;
        if (send(context,link->reply,sizeof(link->reply))==ATLAS_OK) ++link->state.replies;
        else ++link->state.tx_errors;
        return; /* At most one bounded UART write per service iteration. */
    }
    if (link->state.monitoring && !link->state.waiting &&
        (uint32_t)(now-link->probe_ms)>=ATLAS_RADIO_INTERVAL_MS)
        (void)start(link,now,false,send,context);
}

/** @brief Copy counters and calculate the age of real acknowledgement evidence.
 * @param link State. @param now Milliseconds. @param snapshot Destination. */
void AtlasRadioLink_Snapshot(const AtlasRadioLink *link, uint32_t now, AtlasRadioLinkSnapshot *snapshot)
{
    *snapshot=link->state;
    snapshot->ack_age_ms=link->have_ack?(uint32_t)(now-link->ack_ms):UINT32_MAX;
    snapshot->connected=link->have_ack && snapshot->ack_age_ms<ATLAS_RADIO_FRESH_MS;
}
