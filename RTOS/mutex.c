/**
 ******************************************************************************
 * @file    RTOS/mutex.c
 * @brief   COS RTOS 互斥锁实现 — 支持优先级继承与所有权检查
 ******************************************************************************
 */

#include "mutex.h"

// ==================== 互斥锁 API（支持优先级继承） ====================

void mutex_init(MUTEX_ADDR* mtx)
{
    sem_init(&mtx->sem, 1, 1);          // 二值信号量，初始可用（count=1=未锁）
    mtx->holder               = NULL;   // 无持有者
    mtx->holder_original_prio = 0;      // 占位，加锁时写入
}

int mutex_lock(MUTEX_ADDR* mtx, uint32_t timeout_ms)
{
    // ---- 路径 A：锁空闲，直接获取 ----
    if (mtx->holder == NULL) {
        __disable_irq();
        // 双重检查：关中断前可能被 PendSV 切走、其他任务获取了锁
        if (mtx->holder == NULL) {
            /* 关键：消费底层信号量，将 count 从 1 变为 0。
             * 否则 sem.count 还是 1，下一个调用 sem_wait 的任务会
             * 直接消费掉 count 然后覆盖 holder，把锁"偷"走！ */
            mtx->sem.count = 0;                         // 标记信号量为"已占用"
            mtx->holder = current_task;                 // 记录持有者
            mtx->holder_original_prio = current_task->priority; // 保存原始优先级
            current_task->held_mutex = mtx;             // 任务记录自己持了哪把锁
            __enable_irq();
            return 0;                                   // 成功获取
        }
        __enable_irq();
        // 如果双重检查时发现已被抢占获取，继续走下面的路径 B 或 C
    }

    // ---- 路径 B：锁已被当前任务持有 → 死锁检测 ----
    if (mtx->holder == current_task) {
        /* 通常意味着代码逻辑错误（同一个任务试图重复获取同一把锁）。
         * FreeRTOS 可以配置递归互斥锁，但这里简化处理：返回 -1。 */
        return -1;
    }

    /* ---- 路径 C：锁被其他任务持有 → 竞争 ----
     * 在阻塞之前先做优先级继承检查。这一步是解决优先级反转的关键：
     * 如果等待者（当前任务）的优先级高于持有者的优先级，临时将持有者
     * 提升到等待者的优先级，让持有者能尽快执行完临界区、释放锁。 */
    __disable_irq();

    // 再次确认锁还是被别人持有（可能在关中断前被释放了）
    if (mtx->holder != NULL && mtx->holder != current_task) {
        /* ---- 优先级继承核心 ----
         * 条件：等待者比持有者更紧急（优先级数值更大）。
         * 动作：提升持有者的 priority 字段到与等待者相同。
         * 效果：调度器的位图 + CLZ 会立即"看到"持有者优先级变高了，
         *       所有优先级低于新值的任务都无法再抢占持有者。
         * 注意：不修改 base_priority，解锁时要恢复。 */
        if (current_task->priority > mtx->holder->priority) {
            mtx->holder->priority = current_task->priority;
        }
    }
    __enable_irq();

    /* 在底层二值信号量上阻塞等待。
     * 当持有者调用 mutex_unlock 时，sem_signal 会唤醒本任务。
     * 被唤醒后，信号量被消费（count 已从 1→0），
     * 但 mutex_unlock 已经将 holder 置为 NULL，所以本任务
     * 会循环回到函数开头的"路径 A"重新获取。 */
    if (sem_wait(&mtx->sem, timeout_ms) != 0) {
        return -1;                                      // 超时，没拿到锁
    }

    /* 成功拿到锁（被 sem_signal 唤醒或 sem_wait 直接消费了计数）。
     * 现在登记为合法持有者。 */
    __disable_irq();
    mtx->holder = current_task;                         // 成为新持有者
    mtx->holder_original_prio = current_task->priority; // 记录加锁时的优先级
    current_task->held_mutex = mtx;                     // 记录任务持了哪把锁
    __enable_irq();

    return 0;                                           // 成功获取
}

void mutex_unlock(MUTEX_ADDR* mtx)
{
    // 防御性检查：不是持有者不能解锁
    if (mtx->holder != current_task) {
        return;                                         // 静默忽略错误调用
    }

    __disable_irq();

    /* ---- 优先级恢复 ----
     * 将当前任务的优先级恢复到加锁前的原始值。
     * 如果持有期间被多次提升（多个更高优先级的任务等待过），
     * 这里只恢复到加锁时的 base_priority（简化处理）。
     * 注意：如果任务持有另一把锁时被提升了，恢复后另一把锁
     * 的继承关系仍然有效（另一把锁解锁时也会做同样恢复）。 */
    current_task->priority  = current_task->base_priority;
    current_task->held_mutex = NULL;                    // 清除持锁记录
    mtx->holder              = NULL;                    // 锁变为空闲

    __enable_irq();

    // 释放底层信号量 → 唤醒等待队列中最高优先级的任务
    sem_signal(&mtx->sem);
}
