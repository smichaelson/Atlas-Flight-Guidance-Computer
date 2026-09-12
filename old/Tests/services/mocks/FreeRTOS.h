/** @file FreeRTOS.h @brief Deterministic service-test model, NOT a real scheduler.
 * Major definitions: static queue storage and short task-context critical sections. */
#ifndef ATLAS_SERVICE_FREERTOS_H
#define ATLAS_SERVICE_FREERTOS_H
#include <assert.h>
#include <stdint.h>
typedef uint32_t TickType_t, StackType_t, UBaseType_t;
typedef int BaseType_t;
typedef struct { unsigned unused; } StaticTask_t;
typedef StaticTask_t *TaskHandle_t;
typedef struct { uint8_t *memory; unsigned size, capacity, head, tail, count; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY UINT32_MAX
#define configASSERT(condition) assert(condition)
void AtlasService_CriticalEnter(void);
void AtlasService_CriticalExit(void);
#define taskENTER_CRITICAL() AtlasService_CriticalEnter()
#define taskEXIT_CRITICAL() AtlasService_CriticalExit()
#endif
