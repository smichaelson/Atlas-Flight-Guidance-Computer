/**
 * @file FreeRTOS.h
 * @brief Minimal 1 kHz tick model for the review's minimum-delay probe.
 *
 * Major definitions: TickType_t, pdMS_TO_TICKS(), task-context assertion.
 * This is not a FreeRTOS replacement or a scheduler integration test.
 */
#ifndef ATLAS_REVIEW_FREERTOS_H
#define ATLAS_REVIEW_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * configTICK_RATE_HZ) / 1000U))
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ (1000U)
#endif
#define portMAX_DELAY UINT32_MAX
void AtlasReview_AssertFailed(void);
uint32_t AtlasReview_Ipsr(void);
#define configASSERT(condition) do { if (!(condition)) AtlasReview_AssertFailed(); } while (0)
#define __get_IPSR() AtlasReview_Ipsr()
#endif
