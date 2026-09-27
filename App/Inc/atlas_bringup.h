/**
 * @file atlas_bringup.h
 * @brief Isolated RTOS bench application, USB diagnostics and staged peripheral tests.
 * Major functions: AtlasBringup_Start transfers a prepared board to static owners.
 * This entry point never enables the flight hook or pyro control. Only the
 * separate ServoBench profile permits its reviewed bench servo operations.
 */
#ifndef ATLAS_BRINGUP_H
#define ATLAS_BRINGUP_H
#include "atlas_board.h"
/** @brief Start the diagnostic scheduler after generated HAL/core setup.
 * @param board Board_Init result in the ATLAS_BRINGUP profile (modules unprobed).
 * @param watchdog Initialized IWDG; refreshed only while diagnostic tasks progress.
 * @return An error only on failed startup/scheduler return; otherwise never returns.
 * @note Both bench profiles probe onboard sensors and broadcast read-only telemetry
 *       on boot. ServoBench also reads saved stabilization settings. Other tests remain explicit.
 *       Expected missing devices are reported, not hidden by a reset loop. */
AtlasStatus AtlasBringup_Start(AtlasBoard *board, IWDG_HandleTypeDef *watchdog);
#endif
