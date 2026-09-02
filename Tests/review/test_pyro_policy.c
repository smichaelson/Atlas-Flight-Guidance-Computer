/**
 * @file test_pyro_policy.c
 * @brief Inert authorization, freshness, bounded-retry and time-wrap regression tests.
 * Major functions: main runs state-machine scenarios; observe updates mock ADC age.
 * No HAL, GPIO, timer or physical device is linked into this executable.
 */
#include "atlas_pyro_policy.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** @brief Produce qualified mock observations. @param time_ms Mock monotonic time.
 * @return Inputs with all five inert loads reported present. */
static AtlasPyroInput observe(uint32_t time_ms)
{
    AtlasPyroInput input = {0};
    input.now_ms = time_ms;
    input.sample_ms = time_ms;
    input.interlocks_ok = true;
    input.sample_valid = true;
    for (size_t i = 0; i < ATLAS_PYRO_CHANNELS; ++i)
        input.continuity[i] = ATLAS_CONTINUITY_CLOSED;
    return input;
}
/** @brief Require an explicit request and launch exactly one initial action.
 * @param state New state. @param input Valid observation. @param channel Channel. */
static void launch(AtlasPyroPolicy *state, AtlasPyroInput *input, uint8_t channel)
{
    assert(AtlasPyroPolicy_Arm(state, input) == ATLAS_OK);
    assert(AtlasPyroPolicy_Step(state, input) == ATLAS_PYRO_ACTION_NONE);
    assert(AtlasPyroPolicy_Request(state, input, channel) == ATLAS_OK);
    assert(AtlasPyroPolicy_Step(state, input) == ATLAS_PYRO_ACTION_START);
    input->pulse_active = true;
    assert(AtlasPyroPolicy_Step(state, input) == ATLAS_PYRO_ACTION_NONE);
}
/** @brief Test four attempts, each with fresh post-OFF evidence. @param base Clock base. */
static void retry_budget(uint32_t base)
{
    AtlasPyroPolicy state;
    AtlasPyroPolicy_Init(&state);
    AtlasPyroInput input = observe(base);
    launch(&state, &input, 2U);
    for (uint32_t attempt = 1; attempt <= ATLAS_PYRO_MAX_ATTEMPTS; ++attempt)
    {
        assert(state.attempts[2] == attempt);
        input = observe(state.started_ms + 499U);
        input.pulse_active = true;
        assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_NONE);
        input = observe(state.started_ms + 500U);
        input.pulse_complete = true;
        assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_NONE);
        assert(state.phase == ATLAS_PYRO_WAIT_RETRY);
        input = observe(state.finished_ms + 500U);
        assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_NONE);
        input = observe(state.finished_ms + 501U);
        input.sample_ms -= 2U; /* Fresh in general, but taken before OFF guard ended. */
        assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_NONE);
        input.sample_ms = input.now_ms;
        if (attempt < ATLAS_PYRO_MAX_ATTEMPTS)
        {
            assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_START);
            input.pulse_active = true;
        }
        else
        {
            assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_STOP);
            assert(state.phase == ATLAS_PYRO_EXHAUSTED && !state.software_armed);
        }
    }
    AtlasPyroPolicy_Disarm(&state, input.now_ms);
    assert(AtlasPyroPolicy_Arm(&state, &input) == ATLAS_OK);
    assert(AtlasPyroPolicy_Request(&state, &input, 2U) == ATLAS_ERROR_STATE);
    assert(state.attempts[2] == 4U); /* Rearm cannot refill the per-boot budget. */
}
/** @brief Test cancellation, failure, stale ADC and explicit new commands. */
static void failure_cases(void)
{
    AtlasPyroPolicy state;
    AtlasPyroInput input = observe(1000U);
    AtlasPyroPolicy_Init(&state);
    assert(AtlasPyroPolicy_Request(&state, &input, 0U) == ATLAS_ERROR_STATE);
    input.interlocks_ok = false;
    assert(AtlasPyroPolicy_Arm(&state, &input) == ATLAS_ERROR_STATE);
    input = observe(1000U);
    input.sample_ms -= 51U;
    assert(AtlasPyroPolicy_Arm(&state, &input) == ATLAS_ERROR_STATE);
    input = observe(1000U);
    launch(&state, &input, 0U);
    assert(AtlasPyroPolicy_Request(&state, &input, 1U) == ATLAS_ERROR_BUSY);
    input.interlocks_ok = false;
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_STOP);
    assert(!state.software_armed && state.attempts[0] == 1U);

    /* A later explicit command to a DIFFERENT channel also respects global OFF. */
    input = observe(1010U);
    AtlasPyroPolicy_Disarm(&state, input.now_ms);
    assert(AtlasPyroPolicy_Arm(&state, &input) == ATLAS_OK);
    assert(AtlasPyroPolicy_Request(&state, &input, 1U) == ATLAS_OK);
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_NONE);
    input = observe(1510U);
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_NONE);
    input = observe(1511U);
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_START);
    input.pulse_complete = true; /* Stale completion / early hardware cutoff. */
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_STOP);
    assert(state.fault_latched);
    assert(AtlasPyroPolicy_Arm(&state, &input) == ATLAS_ERROR_STATE);

    AtlasPyroPolicy_Init(&state);
    input = observe(100U);
    launch(&state, &input, 0U);
    input = observe(621U);
    input.pulse_active = true;
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_STOP);
    assert(state.fault_latched); /* No completion by independent cutoff deadline. */

    AtlasPyroPolicy_Init(&state);
    input = observe(0U);
    assert(AtlasPyroPolicy_Arm(&state, &input) == ATLAS_OK);
    input.pulse_active = true;
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_STOP);
    assert(state.fault_latched); /* Physical assertion without a request. */
    assert(AtlasPyroPolicy_Step(NULL, &input) == ATLAS_PYRO_ACTION_STOP);
}
/** @brief Test that open/unknown continuity cannot start or silently authorize retries. */
static void continuity_cases(void)
{
    AtlasPyroPolicy state;
    AtlasPyroPolicy_Init(&state);
    AtlasPyroInput input = observe(100U);
    launch(&state, &input, 4U);
    input = observe(600U);
    input.pulse_complete = true;
    (void)AtlasPyroPolicy_Step(&state, &input);
    input = observe(1101U);
    input.continuity[4] = ATLAS_CONTINUITY_OPEN;
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_NONE);
    assert(state.phase == ATLAS_PYRO_COMPLETE && state.attempts[4] == 1U);
    assert(AtlasPyroPolicy_Request(&state, &input, 4U) == ATLAS_ERROR_STATE);
    input.continuity[4] = ATLAS_CONTINUITY_UNKNOWN;
    assert(AtlasPyroPolicy_Request(&state, &input, 4U) == ATLAS_ERROR_STATE);
    input.continuity[4] = ATLAS_CONTINUITY_CLOSED;
    assert(AtlasPyroPolicy_Request(&state, &input, 4U) == ATLAS_OK);
    input.continuity[4] = ATLAS_CONTINUITY_UNKNOWN;
    assert(AtlasPyroPolicy_Step(&state, &input) == ATLAS_PYRO_ACTION_STOP);

    assert(AtlasPyroPolicy_Classify(12000U, 12000U, false, false, 100U, 800U) == ATLAS_CONTINUITY_UNKNOWN);
    assert(AtlasPyroPolicy_Classify(0U, 0U, true, false, 100U, 800U) == ATLAS_CONTINUITY_UNKNOWN);
    assert(AtlasPyroPolicy_Classify(12000U, 0U, true, true, 100U, 800U) == ATLAS_CONTINUITY_UNKNOWN);
    assert(AtlasPyroPolicy_Classify(12000U, 100U, true, false, 100U, 800U) == ATLAS_CONTINUITY_OPEN);
    assert(AtlasPyroPolicy_Classify(12000U, 11500U, true, false, 100U, 800U) == ATLAS_CONTINUITY_CLOSED);
    assert(AtlasPyroPolicy_Classify(12000U, 6000U, true, false, 100U, 800U) == ATLAS_CONTINUITY_UNKNOWN);
    assert(AtlasPyroPolicy_Classify(12000U, 16000U, true, false, 100U, 800U) == ATLAS_CONTINUITY_UNKNOWN);
    assert(AtlasPyroPolicy_Classify(UINT32_MAX, UINT32_MAX, true, false, 100U, 800U) == ATLAS_CONTINUITY_CLOSED);
}
/** @brief Run inert scenarios. @return Zero on success; assertions stop a regression. */
int main(void)
{
    retry_budget(1000U);
    retry_budget(UINT32_MAX - 900U);
    failure_cases();
    continuity_cases();
    puts("PASS: inert pyro policy, four-attempt cap, OFF guard, rearm, interlocks, ADC age, faults and wrap");
    return 0;
}
