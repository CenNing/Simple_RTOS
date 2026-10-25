/**
 ******************************************************************************
 * @file    RTOS/cos.h
 * @brief   COS RTOS 内核头文件 — 类型定义、全局变量声明、API 声明
 * @note    依赖 stm32f10x.h（STM32 标准外设库）
 ******************************************************************************
 */

#ifndef __COS_H__
#define __COS_H__

#include "stm32f10x.h"
#include <stdint.h>
#include <stddef.h>

// ============ 内核对象前向声明（供 TASK_ADDR 使用，完整定义见各模块头文件）============
typedef struct _SEM_ADDR   SEM_ADDR;   // 信号量类型前向声明，完整定义见 sem.h
typedef struct _MUTEX_ADDR MUTEX_ADDR; // 互斥锁类型前向声明，完整定义见 mutex.h
typedef struct _MSG_Q_ADDR MSG_Q_ADDR; // 消息队列类型前向声明，完整定义见 queue.h

// ==================== 任务控制块 TCB ====================

/**
 * @brief  任务控制块，用于保存/恢复任务上下文
 * @note   结构体字段偏移量与 switch.s 中的汇编硬编码偏移严格对应：
 *         r0(0) r1(4) r2(8) ... r12(48) apsr(52) exc_return(56) stack_addr(60)
 *         偏移 64 之后为 C 专用区（汇编不访问，可自由修改）：
 *         task_stack(64)、stack_size(68)、delay_ticks(72)、priority(76)...
 *         修改前 16 个字段（r0~stack_addr）必须同步修改 switch.s 中的偏移量！
 */
typedef struct {
    // ---- 汇编可见区（switch.s 硬编码偏移，严禁增减字段）----
    int r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
    int apsr;
    int* exc_return;            // EXC_RETURN 魔法值，退出异常的钥匙
    int* stack_addr;            // 保存的任务栈指针 PSP

    // ---- C 专用区（汇编不访问，可自由修改）----
    int*       task_stack;      // 指向外部任务栈缓冲区（栈底）
    uint32_t   stack_size;      // 任务栈大小（int 元素个数）
    uint32_t   delay_ticks;     // 剩余延时 tick，0=就绪，>0=延时/阻塞中
    uint8_t    priority;        // 当前优先级（可能因继承而提升）
    uint8_t    base_priority;   // 原始优先级（释放互斥锁时恢复到此值）
    SEM_ADDR*   blocked_sem;     // 正在等待的信号量指针，NULL=未阻塞
    MUTEX_ADDR* held_mutex;      // 当前持有的互斥锁，NULL=未持锁
    MSG_Q_ADDR* blocked_queue;   // 正在等待的消息队列指针，NULL=未阻塞
    uint8_t     blocked_queue_is_send; // 1=阻塞在发送（队满），0=阻塞在接收（队空）
    void*       blocked_queue_data;    // 发送阻塞时的数据指针（接收阻塞时无意义）
} TASK_ADDR;

#define EXC_RETURN  0xFFFFFFFD  // 返回 Thread 模式 + 使用 PSP

// ==================== 优先级常量（FreeRTOS 约定） ====================
#define TASK_PRIO_IDLE      0   // 空闲任务优先级（永远最低）
#define configMAX_PRIORITIES 32 // 最大优先级数（位图 32 位，0~31）

// ==================== 任务数量配置 ====================
#define TASK_COUNT  3           // 2个用户任务（生产者+消费者） + 1个空闲任务

// ==================== 调度器全局变量（extern） ====================
extern TASK_ADDR*  current_task;      // 当前正在运行的任务 TCB 指针
extern TASK_ADDR*  next_task;         // 调度器选中的下一个任务 TCB 指针
extern TASK_ADDR*  task_list[TASK_COUNT]; // 所有任务指针列表
extern uint8_t     task_index;       // 调度器轮转起始序号

extern volatile uint32_t sys_tick_count; // SysTick 毫秒级全局时间戳

// ==================== RTOS 内核 API ====================

void task_init(TASK_ADDR* task, void (*entry)(void), uint8_t priority,
               int* stack_buf, uint32_t stack_size);
void task_exit(void);
void scheduler(void);
void task_start_scheduler(void);
void SysTick_Init(void);
void SysTick_Handler(void);
void task_delay(uint32_t ms);
void task_delay_until(uint32_t* last_wake, uint32_t period_ms);

// ==================== 汇编实现（switch.s） ====================
void task_switch(TASK_ADDR* from_addr, TASK_ADDR* to_addr);
void task_start(TASK_ADDR* to_addr);

#endif // __COS_H__
