/**
 * @file atlas_pyro_policy.c
 * @brief Pure, fail-closed arming/continuity/retry logic for inert unit testing.
 *
 * Major functions: Init, Arm, Disarm, Request, Step and Classify (see public header).
 * Retry budgets survive disarm/rearm. A retry never uses an observation captured
 * before the minimum OFF interval. Unknown continuity cancels, never authorizes.
 */
#include "atlas_pyro_policy.h"
#include <stddef.h>
#include <string.h>

/** @brief Validate current safety observations. @param input Observations.
 * @return true only for fresh, explicitly valid data and interlocks. */
static bool pyro_input_ready(const AtlasPyroInput *input)
{
    return input != NULL && input->interlocks_ok && input->sample_valid &&
           !input->pulse_fault &&
           (uint32_t)(input->now_ms - input->sample_ms) <= ATLAS_PYRO_MAX_SAMPLE_AGE_MS;
}
/** @brief Identify an active request. @param state State. @return Busy indication. */
static bool pyro_busy(const AtlasPyroPolicy *state)
{
    return state->phase == ATLAS_PYRO_PENDING || state->phase == ATLAS_PYRO_FIRING ||
           state->phase == ATLAS_PYRO_WAIT_RETRY;
}
/** @brief Initialize a new boot, disarmed. @param state State. */
void AtlasPyroPolicy_Init(AtlasPyroPolicy *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->channel = UINT8_MAX;
}
/** @brief Explicitly arm without clearing any attempt budget. @param state State.
 * @param input Observations. @return Atlas status. */
AtlasStatus AtlasPyroPolicy_Arm(AtlasPyroPolicy *state, const AtlasPyroInput *input)
{
    if (state == NULL || input == NULL) return ATLAS_ERROR_NULL;
    if (state->fault_latched || !pyro_input_ready(input) || input->pulse_active)
        return ATLAS_ERROR_STATE;
    if (pyro_busy(state)) return ATLAS_ERROR_BUSY;
    state->software_armed = true;
    state->phase = ATLAS_PYRO_IDLE;
    return ATLAS_OK;
}
/** @brief Cancel without clearing budgets, a fault, or the minimum OFF interval.
 * @param state State. @param now_ms Time after hardware was stopped. */
void AtlasPyroPolicy_Disarm(AtlasPyroPolicy *state, uint32_t now_ms)
{
    if (state == NULL) return;
    /* Reconfirming safe hardware after an abort may take time. Always restart
     * the conservative OFF guard; repeated DISARM cannot shorten it. */
    state->finished_ms = now_ms;
    state->off_time_valid = true;
    state->software_armed = false;
    state->phase = state->fault_latched ? ATLAS_PYRO_FAULT : ATLAS_PYRO_CANCELED;
}
/** @brief Accept a fresh, explicitly armed channel request. @param state State.
 * @param input Observations. @param channel Zero-based channel. @return Atlas status. */
AtlasStatus AtlasPyroPolicy_Request(AtlasPyroPolicy *state, const AtlasPyroInput *input, uint8_t channel)
{
    if (state == NULL || input == NULL) return ATLAS_ERROR_NULL;
    if (channel >= ATLAS_PYRO_CHANNELS) return ATLAS_ERROR_ARGUMENT;
    if (!state->software_armed || state->fault_latched || !pyro_input_ready(input))
        return ATLAS_ERROR_STATE;
    if (pyro_busy(state) || input->pulse_active) return ATLAS_ERROR_BUSY;
    if (state->attempts[channel] >= ATLAS_PYRO_MAX_ATTEMPTS ||
        input->continuity[channel] != ATLAS_CONTINUITY_CLOSED) return ATLAS_ERROR_STATE;
    state->channel = channel;
    state->phase = ATLAS_PYRO_PENDING;
    return ATLAS_OK;
}
/** @brief Advance the sequence and require fresh post-cooldown evidence.
 * @param state State. @param input Observations. @return Adapter action. */
AtlasPyroAction AtlasPyroPolicy_Step(AtlasPyroPolicy *state, const AtlasPyroInput *input)
{
    if (state == NULL || input == NULL) return ATLAS_PYRO_ACTION_STOP;
    if (input->pulse_fault || state->fault_latched)
    {
        state->fault_latched = true;
        AtlasPyroPolicy_Disarm(state, input->now_ms);
        return ATLAS_PYRO_ACTION_STOP;
    }
    if (!pyro_input_ready(input) || !state->software_armed)
    {
        const bool stop = pyro_busy(state) || state->software_armed || input->pulse_active;
        if (stop) AtlasPyroPolicy_Disarm(state, input->now_ms);
        return stop ? ATLAS_PYRO_ACTION_STOP : ATLAS_PYRO_ACTION_NONE;
    }
    if (input->pulse_active && state->phase != ATLAS_PYRO_FIRING)
    {
        /* An unrequested or overlapping physical pulse is never normal. */
        state->fault_latched = true;
        AtlasPyroPolicy_Disarm(state, input->now_ms);
        return ATLAS_PYRO_ACTION_STOP;
    }
    if (!pyro_busy(state)) return ATLAS_PYRO_ACTION_NONE;
    if (state->channel >= ATLAS_PYRO_CHANNELS)
    {
        state->fault_latched = true;
        AtlasPyroPolicy_Disarm(state, input->now_ms);
        return ATLAS_PYRO_ACTION_STOP;
    }
    if (state->phase == ATLAS_PYRO_FIRING)
    {
        if (input->pulse_complete)
        {
            if (input->pulse_active ||
                (uint32_t)(input->now_ms - state->started_ms) < ATLAS_PYRO_PULSE_MS)
            {
                state->fault_latched = true;
                AtlasPyroPolicy_Disarm(state, input->now_ms);
                return ATLAS_PYRO_ACTION_STOP;
            }
            /* Measuring OFF from observation, not estimated end, is conservative. */
            state->finished_ms = input->now_ms;
            state->off_time_valid = true;
            state->phase = ATLAS_PYRO_WAIT_RETRY;
        }
        else if (!input->pulse_active ||
                 (uint32_t)(input->now_ms - state->started_ms) > ATLAS_PYRO_PULSE_MS + 20U)
        {
            state->fault_latched = true;
            AtlasPyroPolicy_Disarm(state, input->now_ms);
            return ATLAS_PYRO_ACTION_STOP;
        }
        return ATLAS_PYRO_ACTION_NONE;
    }
    if (state->off_time_valid)
    {
        const uint32_t observed_off = input->sample_ms - state->finished_ms;
        /* One extra millisecond covers HAL-tick phase and a STOP action's bounded
         * register writes. This also applies to NEW commands after disarm/rearm. */
        if ((uint32_t)(input->now_ms - state->finished_ms) <= ATLAS_PYRO_OFF_MS ||
            observed_off >= UINT32_C(0x80000000) || observed_off <= ATLAS_PYRO_OFF_MS)
            return ATLAS_PYRO_ACTION_NONE;
    }
    if (input->continuity[state->channel] == ATLAS_CONTINUITY_OPEN)
    {
        state->phase = state->phase == ATLAS_PYRO_PENDING ?
                       ATLAS_PYRO_CANCELED : ATLAS_PYRO_COMPLETE;
        return ATLAS_PYRO_ACTION_NONE;
    }
    if (input->continuity[state->channel] != ATLAS_CONTINUITY_CLOSED)
    {
        AtlasPyroPolicy_Disarm(state, input->now_ms);
        return ATLAS_PYRO_ACTION_STOP;
    }
    if (state->attempts[state->channel] >= ATLAS_PYRO_MAX_ATTEMPTS)
    {
        state->phase = ATLAS_PYRO_EXHAUSTED;
        state->software_armed = false;
        return ATLAS_PYRO_ACTION_STOP;
    }
    ++state->attempts[state->channel];
    state->started_ms = input->now_ms;
    state->phase = ATLAS_PYRO_FIRING;
    return ATLAS_PYRO_ACTION_START;
}
/** @brief Classify only qualified, non-firing observations. @param supply_mv Arm supply.
 * @param sense_mv Scaled drain. @param valid Observation validity. @param firing Gate state.
 * @param open_max_permille Open threshold. @param closed_min_permille Closed threshold.
 * @return Continuity class, never a claim of initiator resistance or deployment. */
AtlasContinuity AtlasPyroPolicy_Classify(uint32_t supply_mv, uint32_t sense_mv,
    bool valid, bool firing, uint16_t open_max_permille, uint16_t closed_min_permille)
{
    if (!valid || firing || supply_mv == 0U || open_max_permille > 400U ||
        closed_min_permille < 600U || closed_min_permille > 1000U)
        return ATLAS_CONTINUITY_UNKNOWN;
    const uint64_t scaled = (uint64_t)sense_mv * 1000U;
    if (scaled <= (uint64_t)supply_mv * open_max_permille) return ATLAS_CONTINUITY_OPEN;
    if (scaled >= (uint64_t)supply_mv * closed_min_permille &&
        scaled <= (uint64_t)supply_mv * 1200U) return ATLAS_CONTINUITY_CLOSED;
    return ATLAS_CONTINUITY_UNKNOWN;
}
