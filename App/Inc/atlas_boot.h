/**
 * @file atlas_boot.h
 * @brief One-shot reset-to-ROM support for explicit, inhibited USB maintenance.
 * Major functions: EarlyCheck consumes a retained request before HAL/RTOS;
 * RequestDfu requests a system reset; MarkerValid implements the reset policy.
 */
#ifndef ATLAS_BOOT_H
#define ATLAS_BOOT_H
#include <stdbool.h>
#include <stdint.h>
#define ATLAS_BOOT_MAGIC 0x41544655U
/** @brief Validate both marker words and reset cause without hardware access.
 * @param software Software reset occurred. @param power Power/brownout reset occurred.
 * @param first Request word. @param second Complement. @return Accepted request. */
bool AtlasBoot_MarkerValid(bool software, bool power, uint32_t first, uint32_t second);
/** @brief Consume request before MPU, caches, HAL and watchdog initialization.
 * @note Must only run at the start of main, in privileged thread mode. */
void AtlasBoot_EarlyCheck(void);
/** @brief Reset into factory ROM; never returns on target.
 * @note Caller must quiesce work and physically isolate loads. No option bytes
 * or flash are written. The normal application exposes no remote reset parser. */
void AtlasBoot_RequestDfu(void);
#endif
