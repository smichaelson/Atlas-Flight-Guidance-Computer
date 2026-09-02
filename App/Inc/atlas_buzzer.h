/**
 * @file atlas_buzzer.h
 * @brief Differential TIM15 buzzer drive for the Murata PKMCS0909E48H0-R1.
 *
 * Major functions:
 * - AtlasBuzzer_Init(): configures complementary-phase PWM and a silent default.
 * - AtlasBuzzer_Start(): generates a continuous 50-percent differential square wave.
 * - AtlasBuzzer_Beep(): starts a nonblocking, bounded-duration tone.
 * - AtlasBuzzer_Service()/Stop(): end scheduled or continuous tones safely.
 */

#ifndef ATLAS_BUZZER_H
#define ATLAS_BUZZER_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATLAS_BUZZER_RESONANT_FREQUENCY_HZ (4800U)
#define ATLAS_BUZZER_MIN_FREQUENCY_HZ      (1000U)
#define ATLAS_BUZZER_MAX_FREQUENCY_HZ      (10000U)

/** @brief Buzzer driver state. */
typedef struct
{
    TIM_HandleTypeDef *timer;
    uint32_t timer_clock_hz;
    uint32_t stop_at_ms;
    uint32_t frequency_hz;
    bool initialized;
    bool running;
    bool timed;
} AtlasBuzzer;

/**
 * @brief Bind TIM15 and configure opposite-phase channels with no audible output.
 * @param buzzer Destination driver instance.
 * @param timer Initialized TIM15 PWM handle with CH1 on PE5 and CH2 on PE6.
 * @return ATLAS_OK or a typed timer/clock/parameter failure.
 */
AtlasStatus AtlasBuzzer_Init(AtlasBuzzer *buzzer, TIM_HandleTypeDef *timer);

/**
 * @brief Start a continuous differential square wave.
 * @param buzzer Initialized buzzer instance.
 * @param frequency_hz Frequency from 1000 through 10000 Hz; 4800 Hz is resonant.
 * @return ATLAS_OK or a typed argument/timer failure.
 */
AtlasStatus AtlasBuzzer_Start(AtlasBuzzer *buzzer, uint32_t frequency_hz);

/**
 * @brief Start a tone that stops automatically when serviced after its duration.
 * @param buzzer Initialized buzzer instance.
 * @param frequency_hz Frequency from 1000 through 10000 Hz.
 * @param duration_ms Nonzero duration shorter than 2^31 milliseconds.
 * @return ATLAS_OK or a typed argument/timer failure.
 */
AtlasStatus AtlasBuzzer_Beep(AtlasBuzzer *buzzer,
                             uint32_t frequency_hz,
                             uint32_t duration_ms);

/**
 * @brief Stop both TIM15 channels, producing zero differential drive.
 * @param buzzer Initialized buzzer instance; NULL is ignored.
 */
void AtlasBuzzer_Stop(AtlasBuzzer *buzzer);

/**
 * @brief Stop an expired nonblocking beep.
 * @param buzzer Initialized buzzer instance; NULL is ignored.
 */
void AtlasBuzzer_Service(AtlasBuzzer *buzzer);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_BUZZER_H */
