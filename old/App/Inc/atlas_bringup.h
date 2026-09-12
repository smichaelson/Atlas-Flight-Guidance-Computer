/**
 * @file atlas_bringup.h
 * @brief Isolated RTOS bench application, USB diagnostics and staged peripheral tests.
 * Major functions: AtlasBringup_Start transfers a prepared board to static owners.
 * This entry point never enables the flight hook, PWM or pyro control.
 */
#ifndef ATLAS_BRINGUP_H
#define ATLAS_BRINGUP_H
#include "atlas_board.h"
/** @brief Start the diagnostic scheduler after generated HAL/core setup.
 * @param board Board_Init result in the ATLAS_BRINGUP profile (modules unprobed).
 * @param watchdog Initialized IWDG; refreshed only while diagnostic tasks progress.
 * @return An error only on failed startup/scheduler return; otherwise never returns.
 * @note Onboard probes, optional media and external tests require operator commands.
 *       Expected missing devices are reported, not hidden by a reset loop. This
 *       relaxed device-health policy exists only in the non-actuating bench image. */
AtlasStatus AtlasBringup_Start(AtlasBoard *board, IWDG_HandleTypeDef *watchdog);
#endif
