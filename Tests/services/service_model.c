/** @file service_model.c @brief Shared queue/task/GPIO model for inert service tests.
 * Major functions: static copied queues; finite scripted execution of production
 * task loops; explicit context/clock controls. This is not a kernel timing test. */
#include "service_model.h"
#include <setjmp.h>
#include <string.h>
GPIO_TypeDef test_gpio[7];
uint32_t test_tick,test_primask,test_ipsr;
int test_scheduler;
void (*test_task_entry)(void *);
static jmp_buf task_exit;
static unsigned iterations_left,iteration;
static void (*delay_script)(unsigned);
void TestSetPrimask(uint32_t mask) { test_primask=mask; }
void AtlasService_CriticalEnter(void) { }
void AtlasService_CriticalExit(void) { }
uint32_t HAL_GetTick(void) { return test_tick; }
BaseType_t xTaskGetSchedulerState(void) { return test_scheduler; }
TickType_t xTaskGetTickCount(void) { return test_tick; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task;return 500U; }
TaskHandle_t xTaskCreateStatic(void (*entry)(void *),const char *name,uint32_t words,void *argument,
    UBaseType_t priority,StackType_t *stack,StaticTask_t *control)
{ (void)name;(void)words;(void)argument;(void)priority;(void)stack;test_task_entry=entry;return control; }
QueueHandle_t xQueueCreateStatic(UBaseType_t capacity,UBaseType_t size,uint8_t *memory,StaticQueue_t *control)
{ *control=(StaticQueue_t){memory,size,capacity,0U,0U,0U};return control; }
BaseType_t xQueueSend(QueueHandle_t q,const void *item,TickType_t wait)
{ assert(wait==0U);if(q->count==q->capacity)return pdFALSE;memcpy(q->memory+(q->head++%q->capacity)*q->size,item,q->size);++q->count;return pdTRUE; }
BaseType_t xQueuePeek(QueueHandle_t q,void *item,TickType_t wait)
{ assert(wait==0U);if(q->count==0U)return pdFALSE;memcpy(item,q->memory+(q->tail%q->capacity)*q->size,q->size);return pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t q,void *item,TickType_t wait)
{ if(!xQueuePeek(q,item,wait))return pdFALSE;++q->tail;--q->count;return pdTRUE; }
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) { return q->capacity-q->count; }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port,uint16_t pin)
{ return (port->IDR&pin)?GPIO_PIN_SET:GPIO_PIN_RESET; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState state)
{ if(state)port->ODR|=pin;else port->ODR&=~pin; }
/** @brief Reset shared test state; service-specific statics are reset by fixtures. */
void TestRuntimeReset(void)
{
    memset(test_gpio,0,sizeof(test_gpio));test_tick=test_primask=test_ipsr=0U;
    test_scheduler=taskSCHEDULER_NOT_STARTED;test_task_entry=NULL;
}
/** @brief Advance one modeled task delay. @param ticks Delay in 1 kHz test ticks. */
void vTaskDelay(TickType_t ticks)
{
    assert(test_primask==0U && ticks!=0U && iterations_left!=0U);
    test_tick+=ticks;++iteration;
    if(delay_script!=NULL)delay_script(iteration);
    if(--iterations_left==0U)longjmp(task_exit,1);
}
void vTaskDelayUntil(TickType_t *wake,TickType_t period) { *wake+=period;vTaskDelay(period); }
/** @brief Run a captured production task for a finite scripted sequence.
 * @param iterations Loop count. @param script Optional event callback after delays. */
void TestRunTask(unsigned iterations,void (*script)(unsigned))
{
    assert(test_task_entry!=NULL && iterations!=0U);
    iterations_left=iterations;iteration=0U;delay_script=script;test_scheduler=taskSCHEDULER_RUNNING;
    if(setjmp(task_exit)==0)test_task_entry(NULL);
}
