/**
 * @file atlas_storage.c
 * @brief Static low-priority storage owner, append/read lifecycle and valid file time.
 *
 * Major functions:
 * - AtlasStorage_Start()/Submit()/Receive(): bounded, copied-message interface.
 * - storage_execute(): implements mount, unmount, read, append+sync, explicit UTC.
 * - storage_task(): isolates potentially slow removable-media work from control.
 * - AtlasStorage_FatTime(): reads coherent RTC time/date and marks unknown UTC.
 *
 * A write error has an uncertain media outcome and is NEVER automatically retried.
 * No boot write/format occurs. Removal faults unmount the volume; insertion/remount
 * is explicit. All filesystem calls, including future clients, belong here.
 */
#include "atlas_storage.h"
#include "atlas_build.h"
#include "fatfs.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>

#define STORAGE_STACK_WORDS (2048U)
#define STORAGE_RTC_MAGIC (UINT32_C(0x41544C53))
typedef struct { AtlasStorageRequest request; uint32_t ticket; } StorageItem;
static QueueHandle_t requests, results;
static StaticQueue_t request_control, result_control;
static uint8_t request_memory[ATLAS_STORAGE_QUEUE_CAPACITY * sizeof(StorageItem)];
static uint8_t result_memory[ATLAS_STORAGE_QUEUE_CAPACITY * sizeof(AtlasStorageResult)];
static StaticTask_t task_control;
static StackType_t task_stack[STORAGE_STACK_WORDS];
static RTC_HandleTypeDef *storage_rtc;
static AtlasStorageHealth health, published_health;
static uint32_t next_ticket;
static bool started;

/** @brief Test public call context without touching a queue. @return Task readiness. */
static bool storage_context(void)
{
    return started && __get_IPSR() == 0U &&
           xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

/** @brief Validate a short root filename without path traversal. @param name Input.
 * @return true for an optional single 1-3 character extension after a 1-8 base. */
static bool storage_filename_valid(const char name[13])
{
    unsigned base = 0U, extension = 0U;
    bool dot = false;
    for (unsigned i = 0U; i < 13U; ++i)
    {
        const char c = name[i];
        if (c == '\0') return base != 0U && (!dot || extension != 0U);
        if (c == '.' && !dot && base != 0U) { dot = true; continue; }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        if (dot) { if (++extension > 3U) return false; }
        else if (++base > 8U) return false;
    }
    return false;
}

/** @brief Return calendar month length. @param year Year. @param month 1-12.
 * @return Days, or zero for an invalid month. */
static unsigned storage_month_days(unsigned year, unsigned month)
{
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 0U || month > 12U) return 0U;
    return days[month - 1U] + ((month == 2U && year % 4U == 0U) ? 1U : 0U);
}

/** @brief Validate the supported RTC calendar. @param utc Input. @return Validity. */
static bool storage_utc_valid(const AtlasUtc *utc)
{
    return utc->year >= 2000U && utc->year <= 2099U &&
           utc->day != 0U && utc->day <= storage_month_days(utc->year, utc->month) &&
           utc->hour < 24U && utc->minute < 60U && utc->second < 60U;
}

/** @brief Program UTC only on explicit request. @param utc Valid calendar.
 * @return ATLAS status; failed partial updates invalidate the backup marker. */
static AtlasStatus storage_set_utc(const AtlasUtc *utc)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    unsigned days = 0U;
    if (!storage_utc_valid(utc)) return ATLAS_ERROR_ARGUMENT;
    for (unsigned y = 2000U; y < utc->year; ++y) days += y % 4U == 0U ? 366U : 365U;
    for (unsigned m = 1U; m < utc->month; ++m) days += storage_month_days(utc->year, m);
    days += utc->day - 1U;
    date.WeekDay = (uint8_t)((days + 5U) % 7U + 1U); /* 2000-01-01 was Saturday. */
    date.Year = (uint8_t)(utc->year - 2000U);
    date.Month = utc->month; date.Date = utc->day;
    time.Hours = utc->hour; time.Minutes = utc->minute; time.Seconds = utc->second;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;
    HAL_RTCEx_BKUPWrite(storage_rtc, RTC_BKP_DR0, 0U);
    health.time_valid = false;
    memset(&health.utc, 0, sizeof(health.utc));
    health.utc_sampled_at_ms = 0U;
    if (HAL_RTC_SetTime(storage_rtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_SetDate(storage_rtc, &date, RTC_FORMAT_BIN) != HAL_OK) return ATLAS_ERROR_IO;
    HAL_RTCEx_BKUPWrite(storage_rtc, RTC_BKP_DR0, STORAGE_RTC_MAGIC);
    health.time_valid = true;
    return ATLAS_OK;
}

/** @brief Read coherent RTC UTC for FatFs. @return Encoded time or explicit fallback. */
uint32_t AtlasStorage_FatTime(void)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    AtlasUtc utc;
    const uint32_t unknown = (1UL << 21) | (1UL << 16); /* Valid 1980-01-01. */
    health.time_valid = false;
    memset(&health.utc, 0, sizeof(health.utc));
    health.utc_sampled_at_ms = 0U;
    if (storage_rtc == NULL ||
        HAL_RTCEx_BKUPRead(storage_rtc, RTC_BKP_DR0) != STORAGE_RTC_MAGIC) return unknown;
    /* GetDate MUST follow GetTime to unlock the STM32 shadow-register snapshot. */
    const HAL_StatusTypeDef time_status = HAL_RTC_GetTime(storage_rtc, &time, RTC_FORMAT_BIN);
    const HAL_StatusTypeDef date_status = HAL_RTC_GetDate(storage_rtc, &date, RTC_FORMAT_BIN);
    utc = (AtlasUtc){(uint16_t)(2000U + date.Year), date.Month, date.Date,
                     time.Hours, time.Minutes, time.Seconds};
    if (time_status != HAL_OK || date_status != HAL_OK || !storage_utc_valid(&utc)) return unknown;
    health.time_valid = true;
    health.utc = utc;
    health.utc_sampled_at_ms = HAL_GetTick();
    return ((uint32_t)(utc.year - 1980U) << 25) | ((uint32_t)utc.month << 21) |
           ((uint32_t)utc.day << 16) | ((uint32_t)utc.hour << 11) |
           ((uint32_t)utc.minute << 5) | (utc.second / 2U);
}

/** @brief Discard mounted state after errors/removal; no forced retry or formatting. */
static void storage_unmount(void)
{
    (void)f_mount(NULL, SDPath, 0U);
    SD_Invalidate();
    health.mounted = false;
}

/** @brief Probe and mount read-only metadata. @return FatFs result. */
static FRESULT storage_mount(void)
{
    storage_unmount();
    if (retSD != 0U) return FR_NOT_READY;
    SD_PrepareMount();
    FRESULT result = f_mount(&SDFatFS, SDPath, 1U);
    health.mounted = result == FR_OK;
    if (result != FR_OK) storage_unmount();
    return result;
}

#if ATLAS_BRINGUP
/** @brief Deterministic two-sector pattern; NOT an SD endurance/qualification test.
 * @param offset Byte position. @return Expected byte. */
static uint8_t storage_test_byte(uint32_t offset)
{ return (uint8_t)((offset * 37U + (offset >> 8) * 13U + 0xA5U) & 0xFFU); }

/** @brief Explicitly create a NEW test file, sync/close it, then reopen and compare.
 * @param result Receives verified byte count; partial/uncertain files remain for inspection.
 * @return Filesystem result. Existing files are NEVER opened for writing or removed. */
static FRESULT storage_self_test(AtlasStorageResult *result)
{
    FIL file;
    UINT count;
    FRESULT fs = f_open(&file, "0:/" ATLAS_STORAGE_TEST_FILENAME, FA_WRITE | FA_CREATE_NEW);
    if (fs != FR_OK) return fs;
    for (uint32_t offset = 0U; offset < ATLAS_STORAGE_TEST_BYTES && fs == FR_OK; offset += 512U)
    {
        for (uint32_t i = 0U; i < 512U; ++i) result->data[i] = storage_test_byte(offset + i);
        fs = f_write(&file, result->data, 512U, &count);
        if (fs == FR_OK && count != 512U) fs = FR_DENIED;
    }
    if (fs == FR_OK) fs = f_sync(&file);
    FRESULT close_status = f_close(&file);
    if (fs == FR_OK) fs = close_status;
    if (fs != FR_OK) return fs; /* Never retry a possibly committed write. */
    fs = f_open(&file, "0:/" ATLAS_STORAGE_TEST_FILENAME, FA_READ);
    if (fs != FR_OK) return fs;
    if (f_size(&file) != ATLAS_STORAGE_TEST_BYTES) fs = FR_INT_ERR;
    for (uint32_t offset = 0U; offset < ATLAS_STORAGE_TEST_BYTES && fs == FR_OK; offset += 512U)
    {
        fs = f_read(&file, result->data, 512U, &count);
        if (fs == FR_OK && count != 512U) fs = FR_INT_ERR;
        for (uint32_t i = 0U; i < 512U && fs == FR_OK; ++i)
            if (result->data[i] != storage_test_byte(offset + i)) fs = FR_INT_ERR;
    }
    close_status = f_close(&file);
    if (fs == FR_OK) fs = close_status;
    if (fs == FR_OK) result->verified_bytes = ATLAS_STORAGE_TEST_BYTES;
    memset(result->data, 0, sizeof(result->data)); /* No public payload accompanies this result. */
    return fs;
}
#endif

/** @brief Execute one copied request. @param item Request/ticket. @param result Output. */
static void storage_execute(const StorageItem *item, AtlasStorageResult *result)
{
    const AtlasStorageRequest *request = &item->request;
    FRESULT fs = FR_OK;
    FIL file;
    UINT transferred = 0U;
    char path[16] = "0:/";
    memset(result, 0, sizeof(*result));
    result->ticket = item->ticket;
    result->operation = request->operation;
    result->status = ATLAS_OK;
    if (request->operation == ATLAS_STORAGE_MOUNT) fs = storage_mount();
    else if (request->operation == ATLAS_STORAGE_UNMOUNT) storage_unmount();
    else if (request->operation == ATLAS_STORAGE_SET_UTC) result->status = storage_set_utc(&request->utc);
    else if (!health.mounted) fs = FR_NOT_READY;
#if ATLAS_BRINGUP
    else if (request->operation == ATLAS_STORAGE_SELF_TEST) fs = storage_self_test(result);
#endif
    else
    {
        memcpy(path + 3U, request->filename, sizeof(request->filename));
        const bool append = request->operation == ATLAS_STORAGE_APPEND;
        fs = f_open(&file, path, append ? (FA_WRITE | FA_OPEN_ALWAYS) : FA_READ);
        if (fs == FR_OK)
        {
            fs = f_lseek(&file, append ? f_size(&file) : request->offset);
            if (fs == FR_OK)
            {
                if (append)
                {
                    fs = f_write(&file, request->data, request->length, &transferred);
                    /* FR_OK with a short write means full media, not successful append. */
                    if (fs == FR_OK && transferred != request->length) fs = FR_DENIED;
                    if (fs == FR_OK) fs = f_sync(&file);
                }
                else fs = f_read(&file, result->data, request->length, &transferred);
            }
            const FRESULT close_status = f_close(&file);
            if (fs == FR_OK) fs = close_status;
            result->length = (uint16_t)transferred;
        }
    }
    if (fs != FR_OK) result->status = fs == FR_NOT_READY ? ATLAS_ERROR_NOT_READY : ATLAS_ERROR_IO;
    result->filesystem_result = (uint8_t)fs;
    /* Do not keep a cached FAT after an I/O error; card replacement requires remount. */
    if (fs == FR_DISK_ERR || fs == FR_NOT_READY || fs == FR_INT_ERR) storage_unmount();
    health.last_status = result->status;
    health.filesystem_result = result->filesystem_result;
    ++health.completed_requests;
    if (result->status != ATLAS_OK) ++health.errors;
}

/** @brief Run optional media below control/sensor priorities. @param argument Unused. */
static void storage_task(void *argument)
{
    StorageItem item;
    AtlasStorageResult result;
    (void)argument;
    /* On a new PCB, even metadata access waits for the operator's power checks. */
    const FRESULT initial = ATLAS_BRINGUP ? FR_NOT_READY : storage_mount();
    health.filesystem_result = (uint8_t)initial;
    health.last_status = initial == FR_OK ? ATLAS_OK : ATLAS_ERROR_NOT_READY;
    for (;;)
    {
        if (health.mounted && !BSP_SD_IsMediaCurrent())
        {
            storage_unmount();
            health.last_status = ATLAS_ERROR_NOT_READY;
            ++health.errors;
        }
        /* Reserve result capacity before taking a request: never lose a completion.
           An application that stops consuming results gets backpressure, not writes. */
        if (uxQueueSpacesAvailable(results) != 0U &&
            xQueueReceive(requests, &item, 0U) == pdTRUE)
        {
            storage_execute(&item, &result);
            configASSERT(xQueueSend(results, &result, 0U) == pdTRUE);
        }
        if (health.mounted && BSP_SD_GetCardState() == SD_TRANSFER_ERROR)
        {
            storage_unmount();
            health.last_status = ATLAS_ERROR_NOT_READY;
            ++health.errors;
        }
        (void)AtlasStorage_FatTime();
        health.card_detected = BSP_SD_IsDetected() == SD_PRESENT;
        health.stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        taskENTER_CRITICAL();
        published_health = health;
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(10U) != 0U ? pdMS_TO_TICKS(10U) : 1U);
    }
}

/** @brief Create owner and queues before scheduling. @param rtc Handle. @return Status. */
AtlasStatus AtlasStorage_Start(RTC_HandleTypeDef *rtc)
{
    if (rtc == NULL) return ATLAS_ERROR_NULL;
    if (started || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) return ATLAS_ERROR_STATE;
    storage_rtc = rtc;
    requests = xQueueCreateStatic(ATLAS_STORAGE_QUEUE_CAPACITY, sizeof(StorageItem),
                                  request_memory, &request_control);
    results = xQueueCreateStatic(ATLAS_STORAGE_QUEUE_CAPACITY, sizeof(AtlasStorageResult),
                                 result_memory, &result_control);
    if (requests == NULL || results == NULL) return ATLAS_ERROR_STATE;
    if (xTaskCreateStatic(storage_task, "AtlasStorage", STORAGE_STACK_WORDS, NULL, 1U,
                         task_stack, &task_control) == NULL) return ATLAS_ERROR_STATE;
    published_health.last_status = ATLAS_ERROR_NOT_READY;
    started = true;
    return ATLAS_OK;
}

/** @brief Copy a bounded request without waiting. @param request Input.
 * @param ticket Optional ticket. @return Acceptance status (not completion). */
AtlasStatus AtlasStorage_Submit(const AtlasStorageRequest *request, uint32_t *ticket)
{
    StorageItem item;
    if (request == NULL) return ATLAS_ERROR_NULL;
    if (!storage_context()) return ATLAS_ERROR_STATE;
    if ((unsigned)request->operation > (unsigned)ATLAS_STORAGE_SELF_TEST) return ATLAS_ERROR_ARGUMENT;
    if (!ATLAS_BRINGUP && request->operation == ATLAS_STORAGE_SELF_TEST) return ATLAS_ERROR_UNSUPPORTED;
    if ((request->operation == ATLAS_STORAGE_READ || request->operation == ATLAS_STORAGE_APPEND) &&
        (!storage_filename_valid(request->filename) || request->length == 0U ||
         request->length > ATLAS_STORAGE_DATA_CAPACITY)) return ATLAS_ERROR_ARGUMENT;
    if (request->operation == ATLAS_STORAGE_SET_UTC && !storage_utc_valid(&request->utc))
        return ATLAS_ERROR_ARGUMENT;
    item.request = *request;
    taskENTER_CRITICAL();
    item.ticket = ++next_ticket;
    taskEXIT_CRITICAL();
    if (xQueueSend(requests, &item, 0U) != pdTRUE) return ATLAS_ERROR_BUSY;
    if (ticket != NULL) *ticket = item.ticket;
    return ATLAS_OK;
}

/** @brief Copy a completed result without waiting. @param result Destination.
 * @return true when a completion was consumed. */
bool AtlasStorage_Receive(AtlasStorageResult *result)
{
    return result != NULL && storage_context() && xQueueReceive(results, result, 0U) == pdTRUE;
}

/** @brief Copy published service state. @param output Destination. @return Readiness. */
bool AtlasStorage_GetHealth(AtlasStorageHealth *output)
{
    if (output == NULL || !storage_context()) return false;
    taskENTER_CRITICAL();
    *output = published_health;
    taskEXIT_CRITICAL();
    return true;
}
