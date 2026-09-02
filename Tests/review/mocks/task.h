/**
 * @file task.h
 * @brief Task-state and delay declarations for the review tick-phase model.
 *
 * Major functions: xTaskGetSchedulerState(), vTaskDelay().
 */
#ifndef ATLAS_REVIEW_TASK_H
#define ATLAS_REVIEW_TASK_H
#include "FreeRTOS.h"
#define taskSCHEDULER_NOT_STARTED (0)
#define taskSCHEDULER_SUSPENDED (1)
#define taskSCHEDULER_RUNNING (2)
BaseType_t xTaskGetSchedulerState(void);
void vTaskDelay(TickType_t ticks);
#endif
