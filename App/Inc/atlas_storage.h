/**
 * @file atlas_storage.h
 * @brief Single-owner, bounded-request FatFs and UTC service for optional SD media.
 *
 * Major functions:
 * - AtlasStorage_Start(): creates the static low-priority owner before scheduling.
 * - AtlasStorage_Submit()/Receive(): copy requests and ticketed results, never pointers.
 * - AtlasStorage_GetHealth(): reports mount, I/O and time validity.
 * - AtlasStorage_FatTime(): supplies a valid FAT timestamp to the filesystem.
 */
#ifndef ATLAS_STORAGE_H
#define ATLAS_STORAGE_H
#include "main.h"
#include "atlas_status.h"
#include <stdbool.h>
#include <stdint.h>

#define ATLAS_STORAGE_DATA_CAPACITY (512U)
#define ATLAS_STORAGE_QUEUE_CAPACITY (4U)
#define ATLAS_STORAGE_TEST_FILENAME "ATLASCHK.TST"
#define ATLAS_STORAGE_TEST_BYTES (1024U)

/** @brief UTC calendar accepted by the STM32 RTC (2000 through 2099). */
typedef struct
{
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} AtlasUtc;

/** @brief Deliberate media actions; no format, delete, truncate or raw-sector API. */
typedef enum
{
    ATLAS_STORAGE_MOUNT = 0,
    ATLAS_STORAGE_UNMOUNT,
    ATLAS_STORAGE_READ,
    ATLAS_STORAGE_APPEND,
    ATLAS_STORAGE_SET_UTC,
    ATLAS_STORAGE_SELF_TEST /* Bringup image only: exclusive-create, sync, reopen/compare. */
} AtlasStorageOperation;

/** @brief Request copied on submission; root-directory 8.3 filename only. */
typedef struct
{
    AtlasStorageOperation operation;
    char filename[13];
    uint32_t offset;
    uint16_t length;
    uint8_t data[ATLAS_STORAGE_DATA_CAPACITY];
    AtlasUtc utc;
} AtlasStorageRequest;

/** @brief Result copied to one application consumer; correlate by ticket. */
typedef struct
{
    uint32_t ticket;
    AtlasStorageOperation operation;
    AtlasStatus status;
    uint8_t filesystem_result;
    uint16_t length;
    uint32_t verified_bytes; /**< Self-test comparison count, never a data[] length. */
    uint8_t data[ATLAS_STORAGE_DATA_CAPACITY];
} AtlasStorageResult;

/** @brief Last completed operation and optional-media status, not physical presence. */
typedef struct
{
    bool mounted;
    bool card_detected;
    bool time_valid;
    AtlasStatus last_status;
    uint8_t filesystem_result;
    uint32_t completed_requests;
    uint32_t errors;
    uint32_t stack_free_words;
    AtlasUtc utc;
    uint32_t utc_sampled_at_ms;
} AtlasStorageHealth;

/** @brief Create static service before scheduler start; no media accessed here.
 * @param rtc Initialized RTC handle. @return ATLAS status. */
AtlasStatus AtlasStorage_Start(RTC_HandleTypeDef *rtc);
/** @brief Enqueue without waiting, from a running task.
 * @param request Copied input. @param ticket Optional assigned ID.
 * @return ATLAS_OK means accepted, not written/durable. */
AtlasStatus AtlasStorage_Submit(const AtlasStorageRequest *request, uint32_t *ticket);
/** @brief Consume a completed result without waiting; one logical consumer.
 * @param result Destination. @return true when copied. */
bool AtlasStorage_Receive(AtlasStorageResult *result);
/** @brief Copy service health without blocking, from task context.
 * @param health Destination. @return true when copied. */
bool AtlasStorage_GetHealth(AtlasStorageHealth *health);
/** @brief Encode RTC UTC for FatFs; storage-owner only.
 * @return FAT timestamp, or 1980-01-01 when UTC is unknown (health marks invalid). */
uint32_t AtlasStorage_FatTime(void);
#endif
