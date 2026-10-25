/**
 ******************************************************************************
 * @file    RTOS/cos.c
 * @brief   COS RTOS 内核实现 — 任务初始化、FreeRTOS 风格优先级调度器、SysTick、延时 API
 ******************************************************************************
 */

#include "cos.h"

// ==================== 调度器全局变量定义 ====================
TASK_ADDR*  current_task;                     // 当前正运行的任务指针
TASK_ADDR*  next_task;                        // 下一个要运行的任务指针
TASK_ADDR*  task_list[TASK_COUNT];            // 任务指针列表
uint8_t     task_index = 0;                   // 轮转起始序号

volatile uint32_t sys_tick_count = 0;         // 全局毫秒时间戳

// ==================== 任务退出处理 ====================

/**
 * @brief  任务退出处理（任务永远不应返回，若返回则死循环）
 * @note   此函数地址在 task_init() 中填充为异常帧的 LR，
 *         如果任务函数意外 return，PC 会跳转到这里
 */
void task_exit(void)
{
    while (1);
}

// ==================== 任务栈初始化 ====================

/**
 * @brief  初始化任务栈，预先在栈顶填充异常帧
 * @param  task:       任务 TCB 指针
 * @param  entry:      任务入口函数指针
 * @param  priority:   任务优先级（FreeRTOS 约定：0=最低，数值越大越高）
 * @param  stack_buf:  外部分配的任务栈缓冲区（缓冲区的末尾作为初始 SP）
 * @param  stack_size: 栈大小（int 元素个数，不是字节数）
 * @note   在栈顶（stack_buf 末尾的 8 个 int）构造伪造的异常返回帧：
 *         硬件异常返回时自动弹出 R0,R1,R2,R3,R12,LR,PC,xPSR
 *         栈顶 8 int 以下的空间留作任务实际运行时的栈。
 *
 *         不同任务可分配不同大小的栈：
 *         - 简单任务（LED 翻转、空转）：128 int = 512 字节
 *         - 普通任务（串口打印）：      256 int = 1024 字节
 *         - 重计算任务：              512 int = 2048 字节
 */
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

    // SP 现在指向伪造帧起始 R0 位置（stack_buf[stack_size-8]）
    task->stack_addr = sp;              // 保存 PSP 初始值
    task->exc_return = (int*)EXC_RETURN;// 存入 EXC_RETURN 魔法值
}

// ==================== 优先级调度器（单次遍历） ====================

/**
 * @brief  优先级抢占式调度器：单次遍历找最高优先级就绪任务
 * @note   算法：从 task_index+1 开始遍历所有任务，追踪最高就绪优先级
 *         和该优先级的第一个任务。遍历起始偏移保证了同优先级 Round-Robin，
 *         更高优先级任务会覆盖之前的候选。
 *
 *         复杂度 O(n)，对嵌入式场景（TASK_COUNT 通常 < 16）足够高效，
 *         且避免了位图维护开销。
 *
 *         结果存入全局变量 next_task。
 */
void scheduler(void)
{
    uint8_t  best_prio = 0;      // 当前找到的最高就绪优先级（空闲 prio=0 兜底）
    uint8_t  best_idx  = 0;      // 最佳任务索引
    uint8_t  found     = 0;      // 是否已找到至少一个就绪任务

    /* ---- 单次遍历：同时完成"找最高优先级"和"同优先级轮转" ----
     * 从 task_index+1 开始扫描（上次执行任务的下一项），保证同优先级
     * 任务自然 Round-Robin：
     *   - 遇更高优先级 → 覆盖候选（优先级抢占）
     *   - 遇同等优先级且本轮尚未选中 → 选中此任务（轮转）
     *   - 遇同等优先级但本轮已选中 → 跳过（让先遇见的执行）
     */
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

    /* ---- 安全兜底 ----
     * 正常情况下 found 永远为 1（空闲任务 delay_ticks==0，prio==0）。
     * 若因意外未找到，退化为找任意就绪任务。 */
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

// ==================== 启动调度器 ====================

/**
 * @brief  一键启动 RTOS 调度器（调用后不会返回）
 * @note   调用前必须已完成 task_init() 并将所有任务注册到 task_list[]。
 *         内部执行：
 *         1. 将第一个任务设为 current_task
 *         2. 配置 PendSV 为最低优先级
 *         3. 启动 SysTick（1ms 中断）
 *         4. 跳转到第一个任务（永不返回）
 */
void task_start_scheduler(void)
{
    current_task = task_list[0];                    // 第一个任务即为当前任务
    NVIC_SetPriority(PendSV_IRQn, 0xFF);            // PendSV 最低优先级
    SysTick_Init();                                 // 启动 1ms 系统节拍
    task_start(task_list[0]);                       // 跳转第一个任务，不返回
}

// ==================== SysTick 系统节拍 ====================

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

/**
 * @brief  SysTick 中断服务函数（每 1ms 触发）
 * @note   1. 递增全局时间戳 sys_tick_count
 *         2. 递减所有延时任务的 delay_ticks
 *         3. 触发 PendSV 进行任务切换
 */
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

// ==================== 任务延时 API ====================

/* ============================================================
 * task_delay — 相对延时（简单，但有周期漂移）
 * ============================================================
 * 从"此刻"起睡 ms 毫秒，让出 CPU 给其他就绪任务。
 *
 * 用法（先睡再干活）：
 *   while (1) {
 *       task_delay(500);      // 睡 500ms
 *       do_work();            // 干活（耗时 t_work）
 *   }
 *   实际周期 = 500 + t_work → 如果 t_work 波动，周期就不固定。
 *
 * 适合：对周期精度要求不高的任务，写法直观。
 */
void task_delay(uint32_t ms)
{
    current_task->delay_ticks = ms;                 // 设定延时 tick 数
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;            // 立即触发 PendSV 切换
}

/* ============================================================
 * task_delay_until — 绝对延时（补偿执行时间，周期精准）
 * ============================================================
 * 睡到"上次醒来 + period_ms"的绝对时间点，自动扣除任务体耗时。
 *
 * 用法（先干活再补睡）：
 *   uint32_t last_wake = 0;   // 必须是任务静态/全局变量！
 *   while (1) {
 *       do_work();                        // 先干活（耗时 t_work）
 *       task_delay_until(&last_wake, 500); // 补睡 500 - t_work，对齐周期边界
 *   }
 *   实际周期 ≈ 500ms（无论 t_work 如何波动）。
 *
 * 注意：last_wake 变量必须跨循环保持，用 static 局部变量
 *       或任务结构体成员，不能是普通局部变量。
 *       首次调用时 last_wake=0 会立即执行（不延时）。
 */
void task_delay_until(uint32_t* last_wake, uint32_t period_ms)
{
    uint32_t now = sys_tick_count;
    if (*last_wake + period_ms > now) {
        task_delay(*last_wake + period_ms - now);   // 只睡剩余时间
    }
    *last_wake = sys_tick_count;                     // 记录本次醒来时刻
}
