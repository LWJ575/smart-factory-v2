#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ============================================================
 * FreeRTOS 配置 — STM32F103ZET6 精英版
 * 标准 SPL 版本, 不依赖 HAL
 * ============================================================ */

/* ---- 基本配置 ---- */
#define configUSE_PREEMPTION            1      /* 抢占式调度 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0  /* 用通用版选任务 */
#define configUSE_TICKLESS_IDLE         0      /* 不用 tickless 省电 */
#define configCPU_CLOCK_HZ              (72000000UL)  /* 72MHz */
#define configTICK_RATE_HZ              (1000)         /* 1ms tick */
#define configMAX_PRIORITIES            (7)            /* 0-6 */
#define configMINIMAL_STACK_SIZE       ((unsigned short)64)  /* 最小栈 256B */
#define configTOTAL_HEAP_SIZE          ((size_t)(30 * 1024))  /* 30KB 堆 */
#define configMAX_TASK_NAME_LEN         (8)
#define configUSE_16_BIT_TICKS         0      /* 32-bit tick */
#define configIDLE_SHOULD_YIELD         1
#define configUSE_TASK_NOTIFICATION     1
#define configUSE_MUTEXES               1
#define configUSE_RECURSIVE_MUTEXES     0
#define configUSE_COUNTING_SEMAPHORES   0
#define configQUEUE_REGISTRY_SIZE       8
#define configUSE_QUEUE_SETS            0
#define configUSE_TIME_SLICING           1      /* 同优先级时间片轮转 */
#define configUSE_NEWLIB_REENTRANT       0      /* 不用 newlib 可重入 */
#define configENABLE_BACKWARD_COMPATIBILITY 1

/* ---- 内存分配 ---- */
#define configSUPPORT_STATIC_ALLOCATION  0      /* 只用动态分配 */
#define configCHECK_FOR_STACK_OVERFLOW   2      /* 栈溢出检测方式2 */
#define configUSE_MALLOC_FAILED_HOOK     1

/* ---- 钩子函数 ---- */
#define configUSE_IDLE_HOOK              0      /* 不用 idle hook */
#define configUSE_TICK_HOOK              1      /* 用 tick hook (给 LVGL 计时) */
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

/* ---- 运行时统计 ---- */
#define configGENERATE_RUN_TIME_STATS    0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* ---- 协程 ---- */
#define configUSE_CO_ROUTINES            0
#define configMAX_CO_ROUTINE_PRIORITIES  2

/* ---- 软件定时器 ---- */
#define configUSE_TIMERS                 1
#define configTIMER_TASK_PRIORITY        (3)
#define configTIMER_QUEUE_LENGTH         5
#define configTIMER_TASK_STACK_DEPTH     ((unsigned short)128)

/* ---- 中断优先级配置 (STM32F1 特有) ---- */
/* Cortex-M3 有 4 bit 抢占优先级, 0=最高 */
#define configKERNEL_INTERRUPT_PRIORITY          255  /* 最低, PendSV/SysTick */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     191  /* 0xB0, 优先级 >= 11 的中断可调 API */

/* ---- 需要的 FreeRTOS API ---- */
#define INCLUDE_vTaskPrioritySet          1
#define INCLUDE_uxTaskPriorityGet         1
#define INCLUDE_vTaskDelete               1
#define INCLUDE_vTaskSuspend              1
#define INCLUDE_vTaskDelayUntil           1
#define INCLUDE_vTaskDelay                1
#define INCLUDE_xTaskGetSchedulerState    1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

/* ---- 中断处理 ---- */
#define xPortPendSVHandler        PendSV_Handler
#define vPortSVCHandler            SVC_Handler
#define xPortSysTickHandler        SysTick_Handler

/* ---- 原始中断优先级寄存器值 ---- */
#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS __NVIC_PRIO_BITS
#else
  #define configPRIO_BITS 4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   3

#endif /* FREERTOS_CONFIG_H */
