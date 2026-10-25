/**
 ******************************************************************************
 * @file    RTOS/mutex.h
 * @brief   COS RTOS 互斥锁 — 控制块定义与 API 声明（依赖 cos.h、sem.h）
 ******************************************************************************
 */

#ifndef __MUTEX_H__
#define __MUTEX_H__

#include "cos.h"
#include "sem.h"

// ==================== 互斥锁控制块 MUTEX ====================

/**
 * @brief  互斥锁控制块（FreeRTOS 风格，支持优先级继承）
 * @note   与二值信号量的区别：
 *         1. 记录持有者（holder），用于优先级继承和重入检测
 *         2. mutex_lock 时若持有者优先级低于等待者，临时提升持有者优先级
 *         3. mutex_unlock 时恢复持有者原始优先级
 *         4. 只能由持有者解锁（所有权检查）
 */
struct _MUTEX_ADDR {
    SEM_ADDR   sem;                   // 底层二值信号量（count=0=锁住,1=空闲）
    TASK_ADDR* holder;                // 当前持有者，NULL=未锁定
    uint8_t    holder_original_prio;  // 持有者加锁前的原始优先级
};

// ==================== 互斥锁 API ====================

#define MUTEX_WAIT_FOREVER 0xFFFFFFFF // 互斥锁无限等待

void mutex_init  (MUTEX_ADDR* mtx);
int  mutex_lock  (MUTEX_ADDR* mtx, uint32_t timeout_ms);
void mutex_unlock(MUTEX_ADDR* mtx);

#endif // __MUTEX_H__
