#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f10x.h"

#define configUSE_PREEMPTION                    1                          // 是否启用抢占式调度
#define configUSE_IDLE_HOOK                     0                          // 是否启用空闲任务钩子函数
#define configUSE_TICK_HOOK                     0                          // 是否启用系统节拍钩子函数
#define configCPU_CLOCK_HZ                      ( SystemCoreClock )        // CPU 时钟频率
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )    // 系统节拍频率，1000 表示 1ms 一次 Tick
#define configMAX_PRIORITIES                    5                          // 任务可用的最大优先级数量
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )       // 空闲任务的最小栈大小
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 12 * 1024 ) ) // FreeRTOS 堆总大小
#define configMAX_TASK_NAME_LEN                 16                         // 任务名称最大长度
#define configUSE_16_BIT_TICKS                  0                          // 是否使用 16 位 Tick 计数，0 表示使用 32 位
#define configIDLE_SHOULD_YIELD                 1                          // 空闲任务在同优先级下是否主动让出 CPU
#define configUSE_MUTEXES                       1                          // 是否启用互斥量
#define configQUEUE_REGISTRY_SIZE               0                          // 队列注册表大小，0 表示不使用队列注册表
#define configCHECK_FOR_STACK_OVERFLOW          2                          // 栈溢出检测方式，2 表示更严格的检查
#define configUSE_RECURSIVE_MUTEXES             0                          // 是否启用递归互斥量
#define configUSE_MALLOC_FAILED_HOOK            1                          // 内存申请失败时是否调用钩子函数
#define configUSE_APPLICATION_TASK_TAG          0                          // 是否启用任务标签功能
#define configUSE_COUNTING_SEMAPHORES           0                          // 是否启用计数型信号量
#define configGENERATE_RUN_TIME_STATS           0                          // 是否生成运行时间统计信息
#define configUSE_TIME_SLICING                  1                          // 同优先级任务之间是否启用时间片轮转
#define configUSE_NEWLIB_REENTRANT              0                          // 是否为每个任务提供 Newlib 可重入支持
#define configENABLE_BACKWARD_COMPATIBILITY     0                          // 是否启用旧版本 API 兼容
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0                          // 每个任务的线程本地存储指针个数
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1                          // 每个任务的通知数组项数量

#define configSUPPORT_STATIC_ALLOCATION         0                          // 是否支持静态内存分配
#define configSUPPORT_DYNAMIC_ALLOCATION        1                          // 是否支持动态内存分配

#define configPRIO_BITS                         4                          // NVIC 中断优先级位数
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15                         // 可设置的最低中断优先级
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5                     // 可调用 FreeRTOS API 的最高中断优先级

#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) ) // 内核使用的最低中断优先级
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) ) // 可调用内核 API 的最高中断优先级阈值

#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1                          // 是否启用与端口相关的优化任务选择机制

#define configASSERT( x )                      /* 断言失败后关闭中断并停在死循环中，便于调试 */ \
    if( ( x ) == 0 )                          \
    {                                         \
        taskDISABLE_INTERRUPTS();             \
        for( ;; )                             \
        {                                     \
        }                                     \
    }

#define configUSE_CO_ROUTINES                  0                          // 是否启用协程
#define configMAX_CO_ROUTINE_PRIORITIES        1                          // 协程可用的最大优先级数量

#define configUSE_TIMERS                       0                          // 是否启用软件定时器
#define configTIMER_TASK_PRIORITY              1                          // 定时器服务任务优先级
#define configTIMER_QUEUE_LENGTH               4                          // 定时器命令队列长度
#define configTIMER_TASK_STACK_DEPTH           ( configMINIMAL_STACK_SIZE * 2 ) // 定时器服务任务栈深度

#define INCLUDE_vTaskPrioritySet               0                          // 是否包含修改任务优先级的 API
#define INCLUDE_uxTaskPriorityGet              0                          // 是否包含获取任务优先级的 API
#define INCLUDE_vTaskDelete                    1                          // 是否包含删除任务的 API
#define INCLUDE_vTaskSuspend                   0                          // 是否包含挂起任务的 API
#define INCLUDE_xResumeFromISR                 0                          // 是否包含在中断中恢复任务的 API
#define INCLUDE_vTaskDelayUntil                1                          // 是否包含周期延时 API
#define INCLUDE_vTaskDelay                     1                          // 是否包含普通延时 API
#define INCLUDE_xTaskGetSchedulerState         1                          // 是否包含获取调度器状态的 API
#define INCLUDE_xTaskGetIdleTaskHandle         0                          // 是否包含获取空闲任务句柄的 API
#define INCLUDE_xTaskGetCurrentTaskHandle      0                          // 是否包含获取当前任务句柄的 API
#define INCLUDE_uxTaskGetStackHighWaterMark    1                          // 是否包含获取任务栈剩余量的 API

#define xPortPendSVHandler  PendSV_Handler                                   // 将 FreeRTOS PendSV 映射到芯片中断处理函数
#define vPortSVCHandler     SVC_Handler                                      // 将 FreeRTOS SVC 映射到芯片中断处理函数
#define xPortSysTickHandler SysTick_Handler                                  // 将 FreeRTOS SysTick 映射到芯片中断处理函数

#endif
