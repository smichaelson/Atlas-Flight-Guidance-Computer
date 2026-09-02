/**
 * @file atlas_time.h
 * @brief Scheduler-aware timing primitives shared by Atlas hardware drivers.
 *
 * Major functions:
 * - AtlasTime_DelayMs(): yields the running RTOS task or uses HAL timing before startup.
 */

#ifndef ATLAS_TIME_H
#define ATLAS_TIME_H

#include <stdint.h>

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

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_TIME_H */
