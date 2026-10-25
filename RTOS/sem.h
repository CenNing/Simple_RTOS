/**
 ******************************************************************************
 * @file    RTOS/sem.h
 * @brief   COS RTOS 信号量 — 控制块定义与 API 声明（依赖 cos.h）
 ******************************************************************************
 */

#ifndef __SEM_H__
#define __SEM_H__

#include "cos.h"

// ==================== 信号量控制块 SEM ====================

/**
 * @brief  信号量控制块（FreeRTOS 风格）
 * @note   支持二值信号量（max_count=1）和计数信号量（max_count>1）
 *         waiting_tasks 按优先级排序取最高，同优先级 FIFO
 */
struct _SEM_ADDR {
    uint32_t   count;                              // 当前计数值
    uint32_t   max_count;                          // 最大计数值（1=二值信号量）
    TASK_ADDR* waiting_tasks[TASK_COUNT];           // 等待该信号量的任务列表
    uint8_t    waiting_count;                       // 当前等待任务数
};

// ==================== 信号量 API ====================

#define SEM_WAIT_FOREVER 0xFFFFFFFF  // 无限等待（约 49 天，实用等价永久）

void sem_init   (SEM_ADDR* sem, uint32_t max_count, uint32_t initial_count);
int  sem_wait   (SEM_ADDR* sem, uint32_t timeout_ms);
void sem_signal (SEM_ADDR* sem);
void sem_signal_from_isr(SEM_ADDR* sem);

#endif // __SEM_H__
