/**
 * @file atlas_time.h
 * @brief Scheduler-aware timing primitives shared by Atlas hardware drivers.
 *
 * Major functions:
 * - AtlasTime_DelayMs(): yields the running RTOS task or uses HAL timing before startup.
 * - AtlasTime_StartCounter(): owns idempotent startup of the shared BNO/GNSS counter.
 */

#ifndef ATLAS_TIME_H
#define ATLAS_TIME_H

#include <stdint.h>
#include "main.h"
#include "atlas_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Delay for at least the requested number of milliseconds.
 * @param delay_ms Delay duration; zero returns immediately.
 * @note Before the scheduler starts this calls HAL_Delay(). While the scheduler is
 *       running it blocks only the calling task. This function must never run in an ISR.
 */
void AtlasTime_DelayMs(uint32_t delay_ms);

/**
 * @brief Start a configured free-running counter once, without resetting its time.
 * @param timer Configured HAL timer; caller is responsible for its clock/rate.
 * @return ATLAS_OK for a running counter, or a typed null/state/HAL failure.
 * @note Startup/sole-owner context only. A BUSY handle is accepted only with CEN
 *       set. Never stops, reinitializes, or clears a shared timer or capture channel.
 */
AtlasStatus AtlasTime_StartCounter(TIM_HandleTypeDef *timer);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_TIME_H */
