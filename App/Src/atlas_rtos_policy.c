/**
 * @file atlas_rtos_policy.c
 * @brief Deterministic, host-testable scheduling and watchdog policy.
 *
 * Major functions:
 * - AtlasRtosPolicy_PeriodDue(): handles millisecond counter rollover safely.
 * - AtlasRtosPolicy_TimestampFresh(): validates sample age across counter rollover.
 * - AtlasRtosPolicy_EvaluateSupervisor(): fails closed on status, liveness, or stack faults.
 */

#include "atlas_rtos_policy.h"

#include <stddef.h>

/**
 * @brief Report whether a wrap-safe periodic deadline has elapsed.
 * @param now_ms Current monotonic millisecond tick.
 * @param previous_ms Previous action tick.
 * @param period_ms Nonzero period shorter than 2^31 milliseconds.
 * @return true when the action is due.
 */
bool AtlasRtosPolicy_PeriodDue(uint32_t now_ms,
                               uint32_t previous_ms,
                               uint32_t period_ms)
{
    return (period_ms != 0U) &&
           ((uint32_t)(now_ms - previous_ms) >= period_ms);
}

/**
 * @brief Check one sample timestamp against a wrap-safe maximum age.
 * @param now_ms Current monotonic millisecond tick.
 * @param timestamp_ms Sample acquisition/reception tick.
 * @param maximum_age_ms Allowed age.
 * @return true when the sample remains fresh.
 */
bool AtlasRtosPolicy_TimestampFresh(uint32_t now_ms,
                                    uint32_t timestamp_ms,
                                    uint32_t maximum_age_ms)
{
    return (maximum_age_ms < UINT32_C(0x80000000)) &&
           ((uint32_t)(now_ms - timestamp_ms) <= maximum_age_ms);
}

/**
 * @brief Evaluate one watchdog-supervision cycle.
 * @param input Complete supervisor input snapshot.
 * @return The first fault in safety-significant evaluation order.
 */
AtlasRtosFault AtlasRtosPolicy_EvaluateSupervisor(
    const AtlasRtosSupervisorInput *input)
{
    bool io_made_progress;
    bool long_io_within_deadline = false;

    if (input == NULL)
    {
        return ATLAS_RTOS_FAULT_ASSERT;
    }
    if (input->startup_status != ATLAS_OK)
    {
        return ATLAS_RTOS_FAULT_STARTUP;
    }
    if (input->service_status != ATLAS_OK)
    {
        return ATLAS_RTOS_FAULT_BOARD_SERVICE;
    }
    if (input->sampling_status != ATLAS_OK)
    {
        return ATLAS_RTOS_FAULT_SENSOR_SAMPLE;
    }
    io_made_progress = (input->io_heartbeat != input->previous_io_heartbeat);
    if (input->io_busy)
    {
        /* Signed delta is valid because every declared maintenance interval is < 2^31 ms. */
        if ((int32_t)(input->io_busy_until_ms - input->now_ms) <= 0)
        {
            return ATLAS_RTOS_FAULT_IO_DEADLINE;
        }
        long_io_within_deadline = true;
    }
    if (!io_made_progress && !long_io_within_deadline)
    {
        return ATLAS_RTOS_FAULT_IO_STALLED;
    }
    /* A reviewed maintenance transition owns the I/O task and necessarily pauses
       sampling. Freshness becomes mandatory again as soon as it finishes/expires. */
    if (!input->sensors_fresh && !long_io_within_deadline)
    {
        return ATLAS_RTOS_FAULT_SENSOR_STALE;
    }
    if (input->application_heartbeat == input->previous_application_heartbeat)
    {
        return ATLAS_RTOS_FAULT_APPLICATION_STALLED;
    }
    if ((input->minimum_stack_free_words == 0U) ||
        (input->io_stack_free_words < input->minimum_stack_free_words) ||
        (input->application_stack_free_words < input->minimum_stack_free_words) ||
        (input->supervisor_stack_free_words < input->minimum_stack_free_words) ||
        (input->idle_stack_free_words < input->minimum_stack_free_words))
    {
        return ATLAS_RTOS_FAULT_STACK_MARGIN;
    }
    return ATLAS_RTOS_FAULT_NONE;
}
