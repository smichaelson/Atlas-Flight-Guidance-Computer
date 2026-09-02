/**
 * @file test_timing.c
 * @brief Acceptance probe for AtlasTime_DelayMs() at adverse RTOS tick phase.
 *
 * Major functions:
 * - vTaskDelay(): models the earliest legal wake at a 1 kHz tick boundary.
 * - main(): checks zero/pre-scheduler controls and runtime minimum waits.
 *
 * Exit 0 means the tested contract holds; exit 1 exposes open review findings.
 * This links the real atlas_time.c with ATLAS_USE_FREERTOS enabled.
 */
#include "atlas_time.h"
#include "task.h"
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdbool.h>

static uint64_t review_now_us;
static BaseType_t review_scheduler = taskSCHEDULER_NOT_STARTED;
static uint32_t review_ipsr;
static bool expecting_assert;
static jmp_buf assertion_return;
/** @brief Supply a modeled exception number. @return Zero for ordinary task context. */
uint32_t AtlasReview_Ipsr(void) { return review_ipsr; }
/** @brief Capture expected terminal assertions without a Windows crash dialog. */
void AtlasReview_AssertFailed(void)
{
    if (!expecting_assert) { puts("HARNESS ERROR: unexpected timing assertion"); exit(2); }
    longjmp(assertion_return, 1);
}

/**
 * @brief Supply the timer boundary for the linked time module (unused here).
 * @param timer Unused handle.
 * @return HAL_ERROR; this probe exercises delays, not counter startup.
 */
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer)
{
    (void)timer;
    return HAL_ERROR;
}

/** @brief Return the modeled scheduler state. @return Current task state. */
BaseType_t xTaskGetSchedulerState(void) { return review_scheduler; }

/**
 * @brief Model the earliest legal unblock after a relative tick delay.
 * @param ticks Number of future tick increments to wait for.
 * @note A call 999 us into a tick can wake 1 us after a one-tick request.
 */
void vTaskDelay(TickType_t ticks)
{
    const uint64_t tick_us = 1000000U / configTICK_RATE_HZ;
    review_now_us = ((review_now_us / tick_us) + ticks) * tick_us;
}

/** @brief Model the pre-scheduler delay. @param ms Requested milliseconds. */
void HAL_Delay(uint32_t ms) { review_now_us += (uint64_t)ms * 1000U; }

/** @brief Exercise the actual timing primitive. @return Acceptance result. */
int main(void)
{
    const uint32_t waits_ms[] = {1U, 3U, 5U, 10U, 50U};
    unsigned failures = 0U;
    review_now_us = 999U;
    AtlasTime_DelayMs(0U);
    AtlasTime_DelayMs(3U);
    if (review_now_us != 3999U)
    {
        puts("HARNESS ERROR: pre-scheduler/zero-delay control failed");
        return 2;
    }
    puts("CONTROL PASS: zero and pre-scheduler timing");
    review_scheduler = taskSCHEDULER_RUNNING;
    for (unsigned i = 0U; i < sizeof(waits_ms) / sizeof(waits_ms[0]); ++i)
    {
        const uint64_t started_us = (1000000U / configTICK_RATE_HZ) - 1U;
        review_now_us = started_us;
        AtlasTime_DelayMs(waits_ms[i]);
        const uint64_t elapsed = review_now_us - started_us;
        const int failed = elapsed < (uint64_t)waits_ms[i] * 1000U;
        printf("%s R02: requested %lu ms, earliest modeled wait %llu us\n",
               failed ? "FAIL" : "PASS", (unsigned long)waits_ms[i],
               (unsigned long long)elapsed);
        failures += failed ? 1U : 0U;
    }
    review_now_us = 999U;
    AtlasTime_DelayMs(UINT32_MAX);
    if (review_now_us - 999U < (uint64_t)UINT32_MAX * 1000U)
    {
        puts("FAIL R02: maximum-duration tick conversion overflowed");
        ++failures;
    }
    /* Nonzero waits must never block inside an ISR or a suspended scheduler. */
    expecting_assert = true;
    review_ipsr = 16U;
    if (setjmp(assertion_return) == 0) { AtlasTime_DelayMs(1U); ++failures; }
    review_ipsr = 0U;
    review_scheduler = taskSCHEDULER_SUSPENDED;
    if (setjmp(assertion_return) == 0) { AtlasTime_DelayMs(1U); ++failures; }
    expecting_assert = false;
    review_scheduler = taskSCHEDULER_NOT_STARTED;
    review_now_us = 0U;
    AtlasTime_DelayMs(UINT32_MAX);
    if (review_now_us < (uint64_t)UINT32_MAX * 1000U) ++failures;
    printf("PASS timing context/overflow guards at %lu Hz\n", (unsigned long)configTICK_RATE_HZ);
    return failures != 0U ? 1 : 0;
}
