/** @file queue.h @brief Copied FIFO declarations for host service tests.
 * Major functions: create, send, receive, peek and capacity queries. */
#ifndef ATLAS_SERVICE_QUEUE_H
#define ATLAS_SERVICE_QUEUE_H
#include "FreeRTOS.h"
QueueHandle_t xQueueCreateStatic(UBaseType_t capacity, UBaseType_t size, uint8_t *memory, StaticQueue_t *control);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
#endif
