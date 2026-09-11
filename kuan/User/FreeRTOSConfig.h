#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ============================================================
 * FreeRTOS V10.4.6 配置 —— STM32F407 (Cortex-M4F, 168MHz)
 * 编译器: Keil ARMCC V5 (AC5, C90)
 * 说明: 4位中断优先级(0~15), FreeRTOS 要求所有调用 FromISR API
 *       的中断, 其抢占优先级数值必须 >= 5 (数值越小优先级越高)
 * ============================================================ */

#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (168000000ul)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   ((size_t)(32 * 1024))
#define configAPPLICATION_ALLOCATED_HEAP        0

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2

/* 本工程未使用软件定时器(周期上报用任务超时实现) */
#define configUSE_TIMERS                        0

/* Cortex-M4 中断优先级配置 */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                         __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                         4
#endif

/* 内核函数裁剪: 未定义的 INCLUDE_* 在 FreeRTOS.h 中默认为 0,
 * 必须显式打开本工程用到的 API */
#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskDelay              1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_vTaskSuspend            1    /* portMAX_DELAY 阻塞语义依赖 */
#define INCLUDE_xTaskGetSchedulerState  1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_eTaskGetState           0
#define INCLUDE_xTaskAbortDelay         0
#define INCLUDE_xTaskGetHandle          0
#define INCLUDE_xTimerPendFunctionCall  0

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY \
        ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
        ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* 断言失败: 打印文件行号后死循环, 便于接调试器定位 */
extern void vAssertCalled( const char *pcFile, unsigned long ulLine );
#define configASSERT(x) if((x) == 0) vAssertCalled(__FILE__, __LINE__)

#endif /* FREERTOS_CONFIG_H */
