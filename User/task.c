/**
 ******************************************************************************
 * @file    User/task.c
 * @brief   应用任务 — 消息队列演示（生产者 → 消费者）
 *
 * @note    【演示内容】
 *          task5（生产者，prio=2）：每 500ms 发送一条消息到队列，
 *            消息包含序号、时间戳和文本。队满时自动阻塞。
 *          task4（消费者，prio=4）：阻塞等待队列消息，收到后通过
 *            串口打印内容，并翻转 LED（PC13）。
 *
 * @note    【优先级设计】
 *          消费者优先级(4) 高于 生产者优先级(2)：
 *          - 生产者发送消息后，消费者立即被唤醒抢占
 *          - 立即消费 → 队满概率低
 *          - 若想演示队满阻塞，可将生产者间隔缩小到 <<10ms
 *
 * @note    【串口输出示例】
 *          [T5] Send #1: "Hello-1" @t=500ms
 *          [T4] Recv #1: "Hello-1" @t=500ms  LED toggled
 ******************************************************************************
 */

#include "task.h"
#include "uart.h"

// ==================== 任务实例定义 ====================
TASK_ADDR task_4;               // Task4 TCB 实例（消费者）
TASK_ADDR task_5;               // Task5 TCB 实例（生产者）
TASK_ADDR task_idle;            // Idle  TCB 实例

// ==================== 内核对象实例 ====================
MSG_Q_ADDR data_queue;          // 消息队列：生产者→消费者通信

// ==================== 任务栈 ====================
int task4_stack[256];           // 1024 字节 — 消息队列接收 + 打印
int task5_stack[128];           // 512 字节 — 消息队列发送 + 打印
int idle_stack [64];            // 256 字节 — 空转等待

// ==================== 消息队列数据缓冲区 ====================
uint8_t queue_buffer[32 * 8];   // 8 条消息 × 32 字节 = 256 字节

// ==================== 消息格式定义 ====================

/**
 * @brief  队列消息结构体（32 字节定长）
 */
typedef struct {
    uint32_t id;                // 消息序号（从 1 递增）
    uint32_t timestamp;         // 发送时的 sys_tick_count（ms）
    char     text[24];          // 文本负载（'\0' 结尾）
} QueueMsg;

// ==================== Task4 — 消息队列消费者（优先级 4，最高） ====================

/**
 * @brief  Task4 — 高优先级消费者，阻塞等待消息队列
 * @note   收到消息后：串口打印内容 + 翻转 PC13 LED
 *         若队列为空，阻塞让出 CPU 给生产者。
 */
void task4()
{
    QueueMsg msg;

    while (1)
    {
        // 阻塞等待消息（无限等待直到有数据）
        if (queue_receive(&data_queue, &msg, QUEUE_WAIT_FOREVER) == 0) {
            GPIOC->ODR ^= GPIO_Pin_13;          // 翻转 LED，视觉指示收到消息
            safe_printf("[T4] Recv #%d: \"%s\" @t=%dms  LED\r\n",
                       (int)msg.id, msg.text, (int)msg.timestamp);
        }
    }
}

// ==================== Task5 — 消息队列生产者（优先级 2） ====================

/**
 * @brief  Task5 — 中等优先级生产者，周期性向消息队列发送消息
 * @note   每 500ms 发送一条消息（含序号和时间戳）。
 *         队满时阻塞等待，直到消费者取走数据。
 */
void task5()
{
    static uint32_t msg_id = 0;                 // 消息序号（跨循环保持）

    while (1)
    {
        msg_id++;
        QueueMsg msg;
        msg.id        = msg_id;
        msg.timestamp = sys_tick_count;

        // 构造消息文本 "Hello-X"
        msg.text[0] = 'H';
        msg.text[1] = 'e';
        msg.text[2] = 'l';
        msg.text[3] = 'l';
        msg.text[4] = 'o';
        msg.text[5] = '-';
        msg.text[6] = '0' + (char)(msg_id % 10);
        msg.text[7] = '\0';

        safe_printf("[T5] Send #%d: \"%s\" @t=%dms\r\n",
                   (int)msg.id, msg.text, (int)msg.timestamp);

        // 发送消息（队满则等待消费者取走）
        queue_send(&data_queue, &msg, QUEUE_WAIT_FOREVER);

        task_delay(500);                        // 500ms 生产间隔
    }
}

// ==================== Idle Task — 空闲占位 ====================

/**
 * @brief  Idle Task — 空闲任务，永远就绪
 */
void idle_task()
{
    while (1)
    {
        // 空转，可替换为 __WFI() 降低功耗
    }
}
