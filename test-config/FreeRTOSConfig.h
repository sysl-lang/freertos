/* The FreeRTOSConfig.h this package's own test suite is built against.
 *
 * IT IS NOT A CONFIG FOR YOUR APPLICATION, and it is not a default this package imposes -- see
 * README.md: the config is the application's file, which is the whole reason this package declares
 * the kernel rather than vendoring it. This one exists so that `sysl test .` is reproducible, and it
 * is written for FreeRTOS's POSIX port on a development machine.
 *
 * Two settings are here for a reason worth reading, because they are what let the suite need nothing
 * at all of its application:
 *
 *   configKERNEL_PROVIDED_STATIC_MEMORY 1
 *       Under configSUPPORT_STATIC_ALLOCATION the kernel needs the idle and timer tasks' storage from
 *       somewhere, and by default it demands vApplicationGetIdleTaskMemory and
 *       vApplicationGetTimerTaskMemory of the application. This makes the kernel define them itself.
 *       A package cannot supply those hooks -- an @export is one definition of one symbol, so a
 *       package carrying them would collide with the application that has to have them.
 *
 *   configASSERT via <assert.h>
 *       The obvious spelling calls an application function (vAssertCalled), which is the same problem
 *       one layer down. libc's assert needs nobody.
 *
 * The POSIX port's own requirements, which are not obvious and cost a session each:
 *
 *   - configMINIMAL_STACK_SIZE must be at least PTHREAD_STACK_MIN, because a task is a real pthread.
 *   - StackType_t is 8 bytes here, so a stack "depth" is eight times the bytes it would be on a
 *     Cortex-M -- which is why configTOTAL_HEAP_SIZE is megabytes rather than kilobytes.
 *   - xTaskCreate returns pdFAIL silently when the heap is short. A run that hangs with only IDLE
 *     alive is this, and `sample <pid>` is what shows it.
 *
 *   - configSTACK_DEPTH_TYPE is set to unsigned int rather than left at its uint16_t default, because
 *     PTHREAD_STACK_MIN on arm64 macOS is 16384 and two of the kernel's signatures carry a depth in
 *     this type.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <assert.h>
#include <pthread.h>

#define configASSERT( x )                          assert( x )

#define configUSE_PREEMPTION                       1
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configTICK_RATE_HZ                         ( ( TickType_t ) 1000 )
#define configMINIMAL_STACK_SIZE                   ( ( unsigned short ) PTHREAD_STACK_MIN )
#define configTOTAL_HEAP_SIZE                      ( ( size_t ) ( 4 * 1024 * 1024 ) )
#define configMAX_TASK_NAME_LEN                    ( 16 )
#define configUSE_16_BIT_TICKS                     0
#define configIDLE_SHOULD_YIELD                    1
#define configMAX_PRIORITIES                       ( 7 )
#define configSTACK_DEPTH_TYPE                     unsigned int

#define configUSE_MUTEXES                          1
#define configUSE_RECURSIVE_MUTEXES                1
#define configUSE_COUNTING_SEMAPHORES              1
#define configUSE_TASK_NOTIFICATIONS               1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES      3

#define configSUPPORT_STATIC_ALLOCATION            1
#define configSUPPORT_DYNAMIC_ALLOCATION           1
#define configKERNEL_PROVIDED_STATIC_MEMORY        1

#define configUSE_TIMERS                           1
#define configTIMER_TASK_PRIORITY                  ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                   20
#define configTIMER_TASK_STACK_DEPTH               ( ( unsigned short ) PTHREAD_STACK_MIN * 2 )

#define configUSE_TRACE_FACILITY                   1
#define configQUEUE_REGISTRY_SIZE                  8
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS    0
#define configCHECK_FOR_STACK_OVERFLOW             0
#define configUSE_MALLOC_FAILED_HOOK               0

/* The suite reads a task's priority, base priority, state and stack high-water mark, and deletes and
 * suspends tasks -- each of which is behind its own INCLUDE_ switch. */
#define INCLUDE_vTaskPrioritySet                   1
#define INCLUDE_uxTaskPriorityGet                  1
#define INCLUDE_vTaskDelete                        1
#define INCLUDE_vTaskSuspend                       1
#define INCLUDE_vTaskDelayUntil                    1
#define INCLUDE_vTaskDelay                         1
#define INCLUDE_uxTaskGetStackHighWaterMark        1
#define INCLUDE_eTaskGetState                      1
#define INCLUDE_xTaskGetCurrentTaskHandle          1
#define INCLUDE_xTaskGetSchedulerState              1
#define INCLUDE_xSemaphoreGetMutexHolder           1
#define INCLUDE_xTaskAbortDelay                    1

#endif /* FREERTOS_CONFIG_H */
