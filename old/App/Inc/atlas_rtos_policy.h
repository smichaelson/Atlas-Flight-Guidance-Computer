/**
 * @file atlas_rtos_policy.h
 * @brief Hardware-independent timing and watchdog policy for the Atlas RTOS layer.
 *
 * Major functions:
 * - AtlasRtosPolicy_PeriodDue(): performs wrap-safe periodic scheduling checks.
 * - AtlasRtosPolicy_TimestampFresh(): validates sample age across tick rollover.
 * - AtlasRtosPolicy_EvaluateSupervisor(): classifies watchdog supervision failures.
 */

#ifndef ATLAS_RTOS_POLICY_H
#define ATLAS_RTOS_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "atlas_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Latched reasons that intentionally stop the independent watchdog refresh. */
typedef enum
{
    ATLAS_RTOS_FAULT_NONE = 0,
    ATLAS_RTOS_FAULT_STARTUP,
    ATLAS_RTOS_FAULT_BOARD_SERVICE,
    ATLAS_RTOS_FAULT_SENSOR_SAMPLE,
    ATLAS_RTOS_FAULT_SENSOR_STALE,
    ATLAS_RTOS_FAULT_IO_STALLED,
    ATLAS_RTOS_FAULT_APPLICATION_STALLED,
    ATLAS_RTOS_FAULT_APPLICATION_DEADLINE,
    ATLAS_RTOS_FAULT_IO_DEADLINE,
    ATLAS_RTOS_FAULT_STACK_MARGIN,
    ATLAS_RTOS_FAULT_WATCHDOG,
    ATLAS_RTOS_FAULT_ASSERT,
    ATLAS_RTOS_FAULT_STACK_OVERFLOW,
    ATLAS_RTOS_FAULT_SCHEDULER,
    ATLAS_RTOS_FAULT_OUTPUT_SERVICE
} AtlasRtosFault;

/** @brief Inputs needed for one deterministic supervisor decision. */
typedef struct
{
    AtlasStatus startup_status;
    AtlasStatus service_status;
    AtlasStatus sampling_status;
    uint32_t now_ms;
    uint32_t io_heartbeat;
    uint32_t previous_io_heartbeat;
    uint32_t application_heartbeat;
    uint32_t previous_application_heartbeat;
    uint32_t io_busy_until_ms;
    uint32_t io_stack_free_words;
    uint32_t application_stack_free_words;
    uint32_t supervisor_stack_free_words;
    uint32_t idle_stack_free_words;
    uint32_t minimum_stack_free_words;
    bool io_busy;
    bool sensors_fresh;
} AtlasRtosSupervisorInput;

/**
 * @brief Report whether a periodic action is due using unsigned wrap-safe arithmetic.
 * @param now_ms Current monotonic millisecond tick.
 * @param previous_ms Tick recorded for the previous action.
 * @param period_ms Nonzero period shorter than 2^31 milliseconds.
 * @return true when at least period_ms has elapsed.
 */
bool AtlasRtosPolicy_PeriodDue(uint32_t now_ms,
                               uint32_t previous_ms,
                               uint32_t period_ms);

/** @brief Check a complete response deadline relative to the SCHEDULED release.
 * @param now_ticks Current 32-bit kernel tick. @param release_ticks Scheduled release.
 * @param deadline_ticks Nonzero interval below half the counter range.
 * @return true at/after the deadline, or for an invalid interval.
 * @note Caller must not supply a release in the future; subtraction handles wrap. */
bool AtlasRtosPolicy_ResponseLate(uint32_t now_ticks, uint32_t release_ticks,
                                 uint32_t deadline_ticks);

/**
 * @brief Check a timestamp against a wrap-safe maximum age.
 * @param now_ms Current monotonic millisecond tick.
 * @param timestamp_ms Sample acquisition/reception tick.
 * @param maximum_age_ms Allowed age shorter than 2^31 milliseconds.
 * @return true when the sample age is no greater than maximum_age_ms.
 */
bool AtlasRtosPolicy_TimestampFresh(uint32_t now_ms,
                                    uint32_t timestamp_ms,
                                    uint32_t maximum_age_ms);

/**
 * @brief Evaluate all conditions required to refresh the independent watchdog.
 * @param input Complete supervisor input snapshot.
 * @return ATLAS_RTOS_FAULT_NONE only when both tasks progressed, statuses and
 *         required sensor freshness are OK (or freshness is paused by an active,
 *         in-deadline maintenance transition), and all task stacks retain the
 *         configured minimum margin.
 */
AtlasRtosFault AtlasRtosPolicy_EvaluateSupervisor(
    const AtlasRtosSupervisorInput *input);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_RTOS_POLICY_H */
