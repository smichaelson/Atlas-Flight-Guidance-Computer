/**
 * @file atlas_pyro_policy.h
 * @brief Hardware-independent, bounded pyro authorization and retry policy.
 *
 * Major functions:
 * - AtlasPyroPolicy_Init()/Arm()/Disarm(): explicit software-arm lifecycle.
 * - AtlasPyroPolicy_Request()/Step(): one active channel; bounded timed retries.
 * - AtlasPyroPolicy_Classify(): continuity is UNKNOWN without a valid arm supply.
 *
 * This policy NEVER writes a pin. The adapter must provide a pulse cutoff independent
 * of task progress (not a redundant safety circuit) and qualified electrical limits.
 * Timing is one 500 ms pulse,
 * at least 500 ms OFF, then at most THREE retries (four total attempts/channel/boot).
 */
#ifndef ATLAS_PYRO_POLICY_H
#define ATLAS_PYRO_POLICY_H
#include "atlas_status.h"
#include <stdbool.h>
#include <stdint.h>
#define ATLAS_PYRO_CHANNELS (5U)
#define ATLAS_PYRO_PULSE_MS (500U)
#define ATLAS_PYRO_OFF_MS (500U)
#define ATLAS_PYRO_MAX_RETRIES (3U)
#define ATLAS_PYRO_MAX_ATTEMPTS (1U + ATLAS_PYRO_MAX_RETRIES)
#define ATLAS_PYRO_MAX_SAMPLE_AGE_MS (50U)

/** @brief Lead-presence indication, not a measurement of initiator resistance. */
typedef enum { ATLAS_CONTINUITY_UNKNOWN = 0, ATLAS_CONTINUITY_OPEN, ATLAS_CONTINUITY_CLOSED } AtlasContinuity;
/** @brief One serialized firing transaction's observable phase. */
typedef enum
{
    ATLAS_PYRO_IDLE = 0, ATLAS_PYRO_PENDING, ATLAS_PYRO_FIRING,
    ATLAS_PYRO_WAIT_RETRY, ATLAS_PYRO_COMPLETE, ATLAS_PYRO_EXHAUSTED,
    ATLAS_PYRO_CANCELED, ATLAS_PYRO_FAULT
} AtlasPyroPhase;
/** @brief Hardware adapter actions; STOP is idempotent and has priority. */
typedef enum { ATLAS_PYRO_ACTION_NONE = 0, ATLAS_PYRO_ACTION_START, ATLAS_PYRO_ACTION_STOP } AtlasPyroAction;
/** @brief Per-boot state; disarming/configuration changes MUST NOT reset attempts. */
typedef struct
{
    AtlasPyroPhase phase;
    bool software_armed, fault_latched, off_time_valid;
    uint8_t channel;
    uint8_t attempts[ATLAS_PYRO_CHANNELS];
    uint32_t started_ms, finished_ms;
} AtlasPyroPolicy;
/** @brief Fresh observations plus hardware progress; all gates must be true to fire. */
typedef struct
{
    uint32_t now_ms, sample_ms;
    bool interlocks_ok, sample_valid, pulse_active, pulse_complete, pulse_fault;
    AtlasContinuity continuity[ATLAS_PYRO_CHANNELS];
} AtlasPyroInput;

/** @brief Initialize once per boot, always disarmed. @param state Destination. */
void AtlasPyroPolicy_Init(AtlasPyroPolicy *state);
/** @brief Explicitly software-arm with current valid interlocks.
 * @param state State. @param input Observations. @return Atlas status. */
AtlasStatus AtlasPyroPolicy_Arm(AtlasPyroPolicy *state, const AtlasPyroInput *input);
/** @brief Cancel and disarm, preserving budgets/faults and OFF-time history.
 * @param state State. @param now_ms Time AFTER the adapter has made outputs safe.
 * @note The adapter must stop/quiesce hardware before calling this function. */
void AtlasPyroPolicy_Disarm(AtlasPyroPolicy *state, uint32_t now_ms);
/** @brief Request one channel; no pin is asserted here.
 * @param state State. @param input Observations. @param channel Zero-based channel.
 * @return Atlas status; acceptance does not mean successful deployment. */
AtlasStatus AtlasPyroPolicy_Request(AtlasPyroPolicy *state, const AtlasPyroInput *input, uint8_t channel);
/** @brief Advance using new observations; adapter executes the returned action.
 * @param state State. @param input Observations. @return Required hardware action. */
AtlasPyroAction AtlasPyroPolicy_Step(AtlasPyroPolicy *state, const AtlasPyroInput *input);
/** @brief Classify divided/scaled continuity against the armed supply.
 * @param supply_mv Armed-feed millivolts. @param sense_mv Equivalent drain millivolts.
 * @param valid True only with qualified physical-arm supply and fresh ADC.
 * @param firing True while this gate is driven; continuity is then unknowable.
 * @param open_max_permille Qualified open upper ratio (0-400).
 * @param closed_min_permille Qualified closed lower ratio (600-1000).
 * @return UNKNOWN for absent supply, ambiguous, saturated or firing observations. */
AtlasContinuity AtlasPyroPolicy_Classify(uint32_t supply_mv, uint32_t sense_mv,
    bool valid, bool firing, uint16_t open_max_permille, uint16_t closed_min_permille);
#endif
