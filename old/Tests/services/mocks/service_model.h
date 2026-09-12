/** @file service_model.h @brief Scripted, single-task host model; NOT FreeRTOS.
 * Major functions: TestRuntimeReset clears boundary state; TestRunTask advances
 * an actual owner's loop through a finite sequence of explicitly injected events. */
#ifndef ATLAS_SERVICE_MODEL_H
#define ATLAS_SERVICE_MODEL_H
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
extern void (*test_task_entry)(void *);
/** @brief Reset scalar/register model state before service creation. */
void TestRuntimeReset(void);
/** @brief Run the captured owner with simulated delays and a per-iteration script.
 * @param iterations Number of completed iterations. @param script After-delay events.
 * @note setjmp ends the otherwise infinite loop; no concurrent execution is modeled. */
void TestRunTask(unsigned iterations,void (*script)(unsigned));
#endif
