/**
 * @file atlas_telemetry.h
 * @brief Read-only, bounded radio snapshot transport with two erasure-repair packets.
 * Major functions: Init binds identity; Begin copies one snapshot; Feed checks and
 * reassembles packets; Service paces transmission and expires incomplete batches.
 */
#ifndef ATLAS_TELEMETRY_H
#define ATLAS_TELEMETRY_H
#include "atlas_radio_link.h"

#define ATLAS_TM_WORDS 128U
#define ATLAS_TM_BYTES 512U
#define ATLAS_TM_SHARD_BYTES 64U
#define ATLAS_TM_DATA_SHARDS 8U
#define ATLAS_TM_SHARDS 10U
#define ATLAS_TM_FRAME_BYTES 100U
#define ATLAS_TM_INTERVAL_MS 1000U
#define ATLAS_TM_EXPIRY_MS 1500U
#define ATLAS_TM_FRESH_MS 3500U
#define ATLAS_TM_MAGIC UINT32_C(0x31534c54)

/** @brief Stable little-endian uint32 snapshot offsets; final word is CRC-32. */
enum {
    TM_MAGIC=0, TM_UPTIME=1, TM_VERSION=2, TM_PROFILE=3, TM_ATTEMPTED=4,
    TM_INIT=5, TM_COUNT=17, TM_ERRORS=21, TM_SAMPLE_STATUS=25, TM_TIME=29,
    TM_ADXL=33, TM_LSM_ACCEL=36, TM_LSM_GYRO=39, TM_LSM_TEMP=42,
    TM_MMC=43, TM_BARO_PA=46, TM_BARO_TEMP=47, TM_BNO_COUNT=48,
    TM_BNO_TIME=52, TM_BNO_ACCURACY=56, TM_BNO_ACCEL=57, TM_BNO_GYRO=60,
    TM_BNO_MAG=63, TM_BNO_Q=66, TM_GNSS_TIME=70, TM_GNSS_COUNT=71,
    TM_GNSS_FIX=72, TM_LAT=73, TM_LON=74, TM_ALT=75, TM_HACC=76,
    TM_TOW=77, TM_PPS_COUNT=78, TM_PPS_US=79, TM_GNSS_CRC=80,
    TM_ADC_TIME=81, TM_ADC_COUNT=82, TM_ADC_STATUS=83, TM_ADC_VALID=84,
    TM_VDDA=85, TM_DIE_TEMP=86, TM_MV=87, TM_RAW=97, TM_ADC_ERRORS=102,
    TM_RESET=103, TM_POWER_EVENTS=104, TM_FAULT=105, TM_GPIO=106,
    TM_STABILIZATION=107, TM_UP=108, TM_PWM=111, TM_SD=115,
    TM_SD_ERRORS=116, TM_SERVICE=117, TM_BNO_IO=118, TM_BNO_PROTOCOL=119,
    TM_GNSS_UART=120, TM_GNSS_DROPPED=121, TM_TX_PACKETS=122,
    TM_STARTUP=123, TM_CRC=127
};

/** @brief Saturating counters; loss is finalized after a batch closes or a sequence gap. */
typedef struct {
    uint32_t rx_packets, rx_bytes, crc_errors, header_errors, duplicates, old_packets;
    uint32_t foreign_packets, sessions, expected_packets, missing_packets;
    uint32_t good_batches, lost_batches, recovered_batches, recovered_packets, batch_crc_errors;
    uint32_t tx_packets, tx_bytes, tx_errors, tx_batches, tx_aborted;
    uint32_t rx_bps, payload_bps, rx_pps;
} AtlasTelemetryStats;

/** @brief Coherent copy for USB. Sample ages are remote uptimes, never local timestamps. */
typedef struct {
    AtlasTelemetryStats stats;
    uint32_t peer[3], boot[2], sequence, received_ms, assembly_ms, available, tx_ready, streaming;
    uint8_t data[ATLAS_TM_BYTES];
} AtlasTelemetrySnapshot;

/** @brief One sender, one peer, one bounded reassembly; no heap or remote command dispatch. */
typedef struct {
    AtlasTelemetrySnapshot view;
    uint32_t uid[3], boot[2], tx_sequence, tx_began, tx_at;
    uint32_t rx_sequence, rx_began, retired[4][2], retired_next;
    uint32_t rate_at, rate_bytes, rate_packets, rate_payload;
    uint8_t tx[ATLAS_TM_BYTES], assembly[ATLAS_TM_BYTES], parity[2][ATLAS_TM_SHARD_BYTES];
    uint8_t parser[ATLAS_TM_FRAME_BYTES];
    size_t used;
    uint16_t seen;
    uint8_t tx_index;
    bool peer_known, active, closed, delivered, crc_failed;
} AtlasTelemetry;

/** @brief Standard CRC-32/ISO-HDLC. @param data Bytes. @param length Count. @return CRC. */
uint32_t AtlasTelemetry_Crc(const uint8_t *data, size_t length);
/** @brief Reset state. Zero boot disables transmission. @param t State. @param uid Identity.
 * @param boot Random 64-bit boot identifier. @param now Local milliseconds. */
void AtlasTelemetry_Init(AtlasTelemetry *t,const uint32_t uid[3],const uint32_t boot[2],uint32_t now);
/** @brief Pause/resume local broadcast, retaining reception. @param t State. @param enabled Broadcast. */
void AtlasTelemetry_SetStreaming(AtlasTelemetry *t,bool enabled);
/** @brief Request a new snapshot only when the previous batch finished. @param t State.
 * @param now Local milliseconds. @return Time for a new snapshot. */
bool AtlasTelemetry_Due(const AtlasTelemetry *t,uint32_t now);
/** @brief Copy words and append CRC; no pointer escapes. @param t State. @param words Sample.
 * @param now Local milliseconds. @return Accepted. */
bool AtlasTelemetry_Begin(AtlasTelemetry *t,const uint32_t words[ATLAS_TM_WORDS],uint32_t now);
/** @brief Parse arbitrary UART fragments with bounded storage. @param t State.
 * @param bytes Input. @param length Count. @param now Local milliseconds. */
void AtlasTelemetry_Feed(AtlasTelemetry *t,const uint8_t *bytes,size_t length,uint32_t now);
/** @brief Expire batches, calculate rates and send at most one packet per 35 ms.
 * @param t State. @param now Local milliseconds. @param send Callback, NULL skips sending.
 * @param context Callback argument. */
void AtlasTelemetry_Service(AtlasTelemetry *t,uint32_t now,AtlasRadioSend send,void *context);
/** @brief Read a bounded hardware boot nonce before scheduling; zero on any RNG failure.
 * @param boot Two random words, used for reboot separation rather than authentication. */
void AtlasTelemetry_NewBoot(uint32_t boot[2]);
#endif
