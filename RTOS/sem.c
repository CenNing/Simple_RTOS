/**
 ******************************************************************************
 * @file    RTOS/sem.c
 * @brief   COS RTOS 信号量实现 — 二值/计数信号量，支持超时与 ISR 释放
 ******************************************************************************
 */

#include "sem.h"

// ==================== 信号量 API（FreeRTOS 风格） ====================

void sem_init(SEM_ADDR* sem, uint32_t max_count, uint32_t initial_count)
{
    sem->count         = initial_count;             // 初始计数值
    sem->max_count     = max_count;                 // 最大计数值（上限）
    sem->waiting_count = 0;                         // 无等待任务
    // waiting_tasks[] 内容在 waiting_count==0 时无意义，无需初始化
}

/* ---------------------------------------------------------------
   含义 ：等待信号量 ，被任务调用

   参数 ：sem是等待的目标信号量，timeout_ms是超时时间

   返回值 ：0 - 成功等到信号量 ， -1 - 获取信号量失败

   执行逻辑 ：如果count>0,直接消费，count--，返回0，否则再判断超时时间是否为0，若为0，则不等待，直接返回-1
   关闭中断，再次判断信号量是否被外设中断函数释放，若count>0,则中断释放了信号量，直接消费，开启中断，返回0，
   若count=0,则将当前任务放入当前信号量的等待队列，并将timeout_ms装填到当前任务的阻塞计数，开启中断，最后调用PendSV中断，切换任务，
   当前任务进入阻塞态，等待信号量被释放。当sem_signal释放信号量后，会清零当前任务的阻塞计数，或者超时时间达到，当前任务均被唤醒，跳出阻塞态，
   然后判断被唤醒的两种情况，
   情况1，达到超时时间，未获取信号量，返回-1.  
   情况2，sem_signal释放信号量，先关闭中断，遍历等待列表，找到当前任务索引，从索引处依次每个元素前移，完成清除等待列表中的当前任务，再开启中断
   直接消费信号量，返回0。

 */
int sem_wait(SEM_ADDR* sem, uint32_t timeout_ms)
{
    int i, j;                                       // 循环变量（超时清理用）

    // ---- 路径 A：信号量立即可用，直接消费 ----
    if (sem->count > 0) {
        sem->count--;                               // 消费一个信号量
        return 0;                                   // 成功获取
    }

    /* ---- 路径 B：timeout_ms=0，不阻塞，立即返回 ----
     * 等价于 FreeRTOS xSemaphoreTake(sem, 0)：轮询一次 */
    if (timeout_ms == 0) {
        return -1;                                  // 立即可用性检查失败
    }


 /* 双重检查：从第一次判断 count==0 到执行 __disable_irq 之间的窗口内，
     * 在串口，定时器中断等中执行sem_signal_from_isr，count 改为 >0。若不重判，当前任务
     * 会错误进入阻塞。 */
    __disable_irq();  
    if (sem->count > 0) {
        sem->count--;                               // 又有信号量了，直接消费
        __enable_irq();
        return 0;
    }

    // 将当前任务注册到信号量等待列表
    current_task->blocked_sem = sem;
    sem->waiting_tasks[sem->waiting_count] = current_task;
    sem->waiting_count++;

    // 设置阻塞超时（SEM_WAIT_FOREVER=0xFFFFFFFF≈49天，实用等价无限）
    current_task->delay_ticks = timeout_ms;

    __enable_irq();                                 // 退出临界区

    // 触发 PendSV，让出 CPU；当前任务在此处"冻结"
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    // ---- 被唤醒后（PendSV 恢复上下文后从此处继续执行）----
    /* 区分唤醒原因：
     * blocked_sem == NULL      → sem_signal()给当前任务释放信号量，成功获取     
     * blocked_sem != NULL      → delay_ticks 减到 0（超时） */
    if (current_task->blocked_sem != NULL) {
        // ---- 超时路径：从等待列表移除自己 ----
        __disable_irq(); 

        for (i = 0; i < sem->waiting_count; i++)    //遍历等待列表，找到当前任务的索引i
       {
            if (sem->waiting_tasks[i] == current_task)  
            {
                // 移除当前任务，将后续元素前移，保持数组连续
                for (j = i; j < sem->waiting_count - 1; j++)
               {
                    sem->waiting_tasks[j] = sem->waiting_tasks[j + 1];
                }
                sem->waiting_count--;
                break;
            }
        }
        current_task->blocked_sem = NULL;           // 清除阻塞标记
        __enable_irq();
        return -1;                                  // 超时返回
    }

    // blocked_sem 已被 sem_signal 清为 NULL → 成功获取信号量
    return 0;                                       // 成功获取
}

/* ---------------------------------------------------------------
 * 含义 ； 释放信号量，被中断服务程序或者任务调用
   
    参数 ：sem是释放的目标信号量
    返回值 ：无

    执行逻辑： 先关闭中断，再判断是否有任务在等待此信号量，若有，再找出最高优先级的任务，清除该任务的超时时间，唤醒任务。
    若优先级都相等，先唤醒先入队的任务。 若没有，则信号量计数加1，但不超过最大值。开启中断，最后触发PendSV中断，让出cpu。
 */
void sem_signal(SEM_ADDR* sem)
{
    uint8_t   best_prio;
    int       best_idx;      //等待列表最高优先级任务索引
    int       i, j;
    TASK_ADDR* task;

    __disable_irq();                                // 进入临界区

    // 检查是否有任务在等待此信号量
    if (sem->waiting_count > 0) {

        /* ---- 选最高优先级等待任务（FreeRTOS 核心算法）----
         * 遍历等待列表，找 priority 值最大的任务（数值大=优先级高）。
         * 同优先级时，索引小的先入队（FIFO），先被选中。 */
        best_prio = 0;                              // 0=最低优先级
        best_idx  = -1;                             // -1=未找到

        //在等待列表找到优先级最高任务
        for (i = 0; i < sem->waiting_count; i++) {
            if (sem->waiting_tasks[i]->priority > best_prio) {
                best_prio = sem->waiting_tasks[i]->priority;
                best_idx  = i;
            }
        }

        if (best_idx >= 0) {                         //表示至少找到一个任务，即waiting_count>0，即找到了最高优先级任务。
            task = sem->waiting_tasks[best_idx];     //临时存储最高优先级任务

            // 从等待列表中移除此任务
            for (j = best_idx; j < sem->waiting_count - 1; j++) {
                sem->waiting_tasks[j] = sem->waiting_tasks[j + 1];
            }
            sem->waiting_count--;

            // 唤醒任务：清除阻塞标记 + 置为就绪
            task->blocked_sem = NULL;               // 标记：被 signal 唤醒
            task->delay_ticks = 0;                  // 就绪态 → 调度器可选

  
        }
        
    } else {
        // 无等待任务：计数值 +1（但不超过 max_count）
        if (sem->count < sem->max_count) {
            sem->count++;
        }
        
    }

    __enable_irq();                                 // 退出临界区

    // 触发 PendSV：若被唤醒任务优先级高于当前任务，立即抢占
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void sem_signal_from_isr(SEM_ADDR* sem)
{
    uint8_t   best_prio;
    int       best_idx;
    int       i, j;
    TASK_ADDR* task;

    
    __disable_irq();

    if (sem->waiting_count > 0) {
        best_prio = 0;
        best_idx  = -1;

        for (i = 0; i < sem->waiting_count; i++) {
            if (sem->waiting_tasks[i]->priority > best_prio) {
                best_prio = sem->waiting_tasks[i]->priority;
                best_idx  = i;
            }
        }

        if (best_idx >= 0) {
            task = sem->waiting_tasks[best_idx];
            for (j = best_idx; j < sem->waiting_count - 1; j++) {
                sem->waiting_tasks[j] = sem->waiting_tasks[j + 1];
            }
            sem->waiting_count--;
            task->blocked_sem = NULL;
            task->delay_ticks = 0;
        }
    } else {
        if (sem->count < sem->max_count) {
            sem->count++;
        }
    }

    __enable_irq();

    // 在 ISR 上下文中触发 PendSV：CPU 退出所有嵌套 ISR 后统一切换
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}
