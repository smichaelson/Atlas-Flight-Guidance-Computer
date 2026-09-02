/** @file task.h @brief Host-only task interfaces; no host thread or hardware is created.
 * Major functions: state, tick, static creation and cooperative delay hooks. */
#ifndef ATLAS_SERVICE_TASK_H
#define ATLAS_SERVICE_TASK_H
#include "FreeRTOS.h"
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_SUSPENDED 1
#define taskSCHEDULER_RUNNING 2
BaseType_t xTaskGetSchedulerState(void);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, uint32_t words, void *argument,
                             UBaseType_t priority, StackType_t *stack, StaticTask_t *control);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *wake, TickType_t period);
#endif
