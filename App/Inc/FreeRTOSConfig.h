/**
 * @file FreeRTOSConfig.h
 * @brief Static-allocation FreeRTOS configuration for the Atlas STM32H743 flight computer.
 *
 * Major functions and configuration decisions:
 * - 1 kHz preemptive scheduling with eight application priority levels.
 * - Static allocation only; no FreeRTOS heap and no permitted C-library heap growth.
 * - Cortex-M7 FPU support, stack-overflow checking, and project-owned assertion handling.
 * - Native FreeRTOS APIs with generated STM32 exception names mapped to the kernel port.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__ICCARM__) || defined(__GNUC__)
#include <stddef.h>
#include <stdint.h>
extern uint32_t SystemCoreClock;
void AtlasRtos_AssertFailed(const char *file, uint32_t line);
#endif

#define configENABLE_FPU                         1
#define configENABLE_MPU                         0

#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         0
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCPU_CLOCK_HZ                       (SystemCoreClock)
#define configTICK_RATE_HZ                       ((TickType_t)1000U)
#define configMAX_PRIORITIES                     8U
#define configMINIMAL_STACK_SIZE                 ((uint16_t)256U)
#define configMAX_TASK_NAME_LEN                  16U
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            0
#define configQUEUE_REGISTRY_SIZE                8U
#define configUSE_QUEUE_SETS                     0
#define configUSE_TRACE_FACILITY                 0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configGENERATE_RUN_TIME_STATS            0
#define configCHECK_FOR_STACK_OVERFLOW            2
#define configUSE_MALLOC_FAILED_HOOK              0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   0
#define configUSE_TICKLESS_IDLE                   0
#define configUSE_TASK_NOTIFICATIONS              1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES     1U
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   0
#define configMESSAGE_BUFFER_LENGTH_TYPE          size_t
#define configUSE_SB_COMPLETED_CALLBACK            0

#define configUSE_CO_ROUTINES                     0
#define configMAX_CO_ROUTINE_PRIORITIES           1U

/* Atlas uses task delays and queues directly; the software-timer task is unnecessary. */
#define configUSE_TIMERS                          0
#define configTIMER_TASK_PRIORITY                 1U
#define configTIMER_QUEUE_LENGTH                  1U
#define configTIMER_TASK_STACK_DEPTH              256U

#define INCLUDE_vTaskPrioritySet                  0
#define INCLUDE_uxTaskPriorityGet                 0
#define INCLUDE_vTaskDelete                       0
#define INCLUDE_vTaskSuspend                      0
#define INCLUDE_xTaskDelayUntil                   1
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_xTaskGetSchedulerState            1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_xTaskGetIdleTaskHandle            1
#define INCLUDE_xQueueGetMutexHolder              0
#define INCLUDE_eTaskGetState                     0

#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                           __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                           4U
#endif

/*
 * Atlas ISRs currently call no FreeRTOS API. If a future ISR uses a FromISR API,
 * its numerical NVIC priority must be 5 through 15 under PRIORITYGROUP_4.
 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY          15U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY      5U
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8U - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8U - configPRIO_BITS))

#define configASSERT(expression)                                      \
    do                                                                \
    {                                                                 \
        if ((expression) == 0)                                        \
        {                                                             \
            AtlasRtos_AssertFailed(__FILE__, (uint32_t)__LINE__);      \
        }                                                             \
    } while (0)

/* The startup vector retains the CubeMX names while the port supplies the bodies. */
#define vPortSVCHandler                    SVC_Handler
#define xPortPendSVHandler                 PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
