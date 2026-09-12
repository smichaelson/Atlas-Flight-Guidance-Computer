/**
 * @file test_bno085_sh2_stubs.h
 * @brief Deterministic CEVA API boundary used to exercise the Atlas BNO085 HAL.
 *
 * Major functions:
 * - AtlasTest_BnoSh2Begin(): installs a successful or failing virtual BNO085.
 * - AtlasTest_BnoSh2ContractPassed(): reports exact two-interrupt transfer checks.
 * - AtlasTest_BnoSh2End(): removes hooks so unrelated protocol tests stay isolated.
 */

#ifndef ATLAS_TEST_BNO085_SH2_STUBS_H
#define ATLAS_TEST_BNO085_SH2_STUBS_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

/**
 * @brief Reset the virtual hub and install its I2C/GPIO hooks.
 * @param i2c Expected shared-bus handle.
 * @param fail_header true to make every header-read attempt time out.
 */
void AtlasTest_BnoSh2Begin(I2C_HandleTypeDef *i2c, bool fail_header);

/** @brief Return whether edge rearming, staged reads, and address checks passed. */
bool AtlasTest_BnoSh2ContractPassed(void);

/** @brief Return the number of blocking receive calls made to the virtual hub. */
uint32_t AtlasTest_BnoSh2ReceiveCalls(void);

/** @brief Return the full-transfer timeout supplied to the STM32 HAL. */
uint32_t AtlasTest_BnoSh2FullReadTimeoutMs(void);

/** @brief Return the number of writes accepted by the virtual hub. */
uint32_t AtlasTest_BnoSh2WriteCalls(void);

/** @brief Clear virtual-device hooks and retained SH-2 state. */
void AtlasTest_BnoSh2End(void);

#endif /* ATLAS_TEST_BNO085_SH2_STUBS_H */
