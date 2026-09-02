/**
 * @file atlas_time.c
 * @brief Scheduler-aware delay implementation for startup and runtime driver calls.
 *
 * Major functions:
 * - AtlasTime_DelayMs(): selects HAL busy-wait timing before scheduling and task delay after.
 */

#include "atlas_time.h"

#include "main.h"

#if defined(ATLAS_USE_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#endif

/**
 * @brief Delay without busy-spinning a running RTOS task.
 * @param delay_ms Delay duration; zero returns immediately.
 */
void AtlasTime_DelayMs(uint32_t delay_ms)
{
    if (delay_ms == 0U)
    {
        return;
    }

#if defined(ATLAS_USE_FREERTOS)
    /* Driver delays are task-only: blocking inside an exception could deadlock SysTick. */
    configASSERT(__get_IPSR() == 0U);
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        TickType_t ticks = pdMS_TO_TICKS(delay_ms);
        if (ticks == 0U)
        {
            ticks = 1U;
        }
        vTaskDelay(ticks);
        return;
    }
#endif

    HAL_Delay(delay_ms);
}
