#include "cos.h"

// ==================== 调度器全局变量定义 ====================
TASK_ADDR*  current_task;                     // 当前正运行的任务指针
TASK_ADDR*  next_task;                        // 下一个要运行的任务指针
TASK_ADDR*  task_list[TASK_COUNT];            // 任务指针列表
uint8_t     task_index = 0;                   // 轮转起始序号

volatile uint32_t sys_tick_count = 0;         // 全局毫秒时间戳


void task_exit(void)
{
    while (1);
}


void task_init(TASK_ADDR* task, void (*entry)(void), uint8_t priority,
               int* stack_buf, uint32_t stack_size)
{
    // SP 指向栈缓冲区末尾（高地址），栈向下增长
    int* sp = stack_buf + stack_size;
    int i;

    // 初始化结构体中的r0-r12寄存器字段为 0
    for (i = 0; i < 13; i++) {
        (&task->r0)[i] = 0;
    }
    task->apsr   = 0x01000000;          // Thumb 模式
    task->task_stack = stack_buf;       // 记录栈缓冲区指针
    task->stack_size = stack_size;      // 记录栈大小（供溢出检测等用途）
    task->delay_ticks  = 0;             // 初始就绪
    task->priority      = priority;     // 当前优先级（可能被互斥锁提升）
    task->base_priority = priority;     // 原始优先级（释放锁时恢复）
    task->blocked_sem          = NULL;   // 未阻塞在任何信号量上
    task->held_mutex           = NULL;   // 未持有任何互斥锁
    task->blocked_queue        = NULL;   // 未阻塞在任何消息队列上
    task->blocked_queue_is_send = 0;     // 默认标记（无意义当 blocked_queue==NULL）
    task->blocked_queue_data   = NULL;   // 无待发送数据

    // 在栈顶 [end-8, end-1] 构造伪造的异常返回帧（从高地址向低地址压栈）
    *(--sp) = 0x01000000;               // [stack_size-1] xPSR
    *(--sp) = (int)entry;               // [stack_size-2] PC = 任务入口
    *(--sp) = (int)task_exit;           // [stack_size-3] LR = 退出处理
    *(--sp) = 0;                        // [stack_size-4] R12
    *(--sp) = 0;                        // [stack_size-5] R3
    *(--sp) = 0;                        // [stack_size-6] R2
    *(--sp) = 0;                        // [stack_size-7] R1
    *(--sp) = 0;                        // [stack_size-8] R0

    
    task->stack_addr = sp;              // 保存 PSP 初始值
    task->exc_return = (int*)EXC_RETURN;// 存入 EXC_RETURN 魔法值
}



void scheduler(void)
{
    uint8_t  best_prio = 0;      // 当前找到的最高就绪优先级（空闲 prio=0 兜底）
    uint8_t  best_idx  = 0;      // 最佳任务索引
    uint8_t  found     = 0;      // 是否已找到至少一个就绪任务

  
    for (uint8_t i = 0; i < TASK_COUNT; i++) {
        uint8_t idx = (task_index + 1 + i) % TASK_COUNT;

        if (task_list[idx]->delay_ticks == 0) {
            if (task_list[idx]->priority > best_prio) {
                // 找到更高优先级 → 立即切换候选人
                best_prio = task_list[idx]->priority;
                best_idx  = idx;
                found     = 1;
            } else if (task_list[idx]->priority == best_prio && !found) {
                // 同优先级、尚未有候选 → 选当前（轮转语义）
                best_idx = idx;
                found    = 1;
            }
            // priority < best_prio → 忽略
        }
    }
    if (!found) {
        for (uint8_t i = 0; i < TASK_COUNT; i++) {
            if (task_list[i]->delay_ticks == 0) {
                best_idx = i;
                break;
            }
        }
    }

    task_index = best_idx;
    next_task  = task_list[best_idx];
}



void task_start_scheduler(void)
{
    current_task = task_list[0];                    // 第一个任务即为当前任务
    NVIC_SetPriority(PendSV_IRQn, 0xFF);            // PendSV 最低优先级
    SysTick_Init();                                 // 启动 1ms 系统节拍
    task_start(task_list[0]);                       // 跳转第一个任务，不返回
}


/**
 * @brief  初始化 SysTick，配置为每 1ms 中断一次
 * @note   使用 SystemCoreClock / 1000 作为重装载值
 */
void SysTick_Init(void)
{
    if (SysTick_Config(SystemCoreClock / 1000))     // 1ms 中断周期
    {
        while (1);                                  // 配置失败则死循环
    }
}

 
void SysTick_Handler(void)
{
    sys_tick_count++;                               // 全局时间戳 +1ms

    // 递减所有延时任务的计数器
    for (int i = 0; i < TASK_COUNT; i++) {
        if (task_list[i]->delay_ticks > 0) {
            task_list[i]->delay_ticks--;
        }
    }
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;            // 触发 PendSV 任务切换
}


void task_delay(uint32_t ms)
{
    current_task->delay_ticks = ms;                 // 设定延时 tick 数
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;            // 立即触发 PendSV 切换
}

void task_delay_until(uint32_t* last_wake, uint32_t period_ms)
{
    uint32_t now = sys_tick_count;
    if (*last_wake + period_ms > now) {
        task_delay(*last_wake + period_ms - now);   // 只睡剩余时间
    }
    *last_wake = sys_tick_count;                     // 记录本次醒来时刻
}
