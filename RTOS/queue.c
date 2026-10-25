/**
 ******************************************************************************
 * @file    RTOS/queue.c
 * @brief   COS RTOS 消息队列实现 — 环形缓冲、按值拷贝、直接传递
 ******************************************************************************
 */

#include "queue.h"

// ==================== 消息队列 API（FreeRTOS 风格，按值拷贝） ====================

void queue_init(MSG_Q_ADDR* q, void* buffer, uint32_t msg_size, uint32_t queue_length)
{
    q->buffer             = (uint8_t*)buffer;     // 缓冲区指针
    q->msg_size           = msg_size;              // 每条消息大小（字节）
    q->queue_length       = queue_length;          // 最大消息条数
    q->count              = 0;                     // 初始为空
    q->head               = 0;                     // 写索引从头开始
    q->tail               = 0;                     // 读索引从头开始
    q->recv_waiting_count = 0;                     // 无等待接收的任务
    q->send_waiting_count = 0;                     // 无等待发送的任务
}

static TASK_ADDR* queue_wake_highest(TASK_ADDR* waiting_tasks[], uint8_t* waiting_count)
{
    uint8_t best_prio;
    int     best_idx;
    int     i, j;
    TASK_ADDR* task;

    if (*waiting_count == 0) return NULL;

    best_prio = 0;
    best_idx  = -1;
    for (i = 0; i < *waiting_count; i++) {
        if (waiting_tasks[i]->priority > best_prio) {
            best_prio = waiting_tasks[i]->priority;
            best_idx  = i;
        }
    }

    if (best_idx < 0) return NULL;

    task = waiting_tasks[best_idx];
    for (j = best_idx; j < *waiting_count - 1; j++) {
        waiting_tasks[j] = waiting_tasks[j + 1];
    }
    (*waiting_count)--;

    /* 清除阻塞标记，该任务变为就绪
     * 注意：blocked_queue_data 不清除！由调用者在拷贝数据后再置 NULL */
    task->blocked_queue        = NULL;
    task->blocked_queue_is_send = 0;
    task->delay_ticks          = 0;

    return task;
}

int queue_send(MSG_Q_ADDR* q, const void* data, uint32_t timeout_ms)
{
    uint32_t offset;
    int      i, j;

    __disable_irq();

    /* ---- 路径 A：有接收者阻塞等待（此时队列必为空）----
     * 直接将数据写入环形缓冲区，然后唤醒接收者。 */
    if (q->recv_waiting_count > 0) {
        offset = (q->head) * (q->msg_size);
        for (uint32_t k = 0; k < q->msg_size; k++) {
            q->buffer[offset + k] = ((const uint8_t*)data)[k];
        }
        q->head = (q->head + 1) % q->queue_length;
        q->count++;

        TASK_ADDR* receiver = queue_wake_highest(q->recv_waiting_tasks,
                                                  &q->recv_waiting_count);
        (void)receiver;
        __enable_irq();
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        return 0;
    }

    // ---- 路径 B：队未满，直接写入环形缓冲区 ----
    if (q->count < q->queue_length) {
        offset = q->head * q->msg_size;
        for (uint32_t k = 0; k < q->msg_size; k++) {
            q->buffer[offset + k] = ((const uint8_t*)data)[k];
        }
        q->head = (q->head + 1) % q->queue_length;
        q->count++;
        __enable_irq();
        return 0;
    }

    /* ---- 路径 C：队满 ----
     * 双重检查：从队满判断到关中断窗口内，可能已有接收者取走数据 */
    if (timeout_ms == 0) {
        __enable_irq();
        return -1;
    }

    // 注册到发送等待列表，同时保存数据指针供接收者唤醒时拷贝
    current_task->blocked_queue         = q;
    current_task->blocked_queue_is_send = 1;
    current_task->blocked_queue_data    = (void*)data;     // 关键：保存数据指针
    q->send_waiting_tasks[q->send_waiting_count] = current_task;
    q->send_waiting_count++;
    current_task->delay_ticks = timeout_ms;

    __enable_irq();

    // 让出 CPU，等待接收者取数据后唤醒
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    // ---- 被唤醒后 ----
    if (current_task->blocked_queue != NULL) {
        // 超时：从发送等待列表移除自己
        __disable_irq();
        for (i = 0; i < q->send_waiting_count; i++) {
            if (q->send_waiting_tasks[i] == current_task) {
                for (j = i; j < q->send_waiting_count - 1; j++) {
                    q->send_waiting_tasks[j] = q->send_waiting_tasks[j + 1];
                }
                q->send_waiting_count--;
                break;
            }
        }
        current_task->blocked_queue  = NULL;
        current_task->blocked_queue_is_send = 0;
        current_task->blocked_queue_data    = NULL;
        __enable_irq();
        return -1;
    }

    // 被接收者唤醒 → 数据已由接收者拷入环形缓冲区 → 发送成功
    return 0;
}

int queue_receive(MSG_Q_ADDR* q, void* data, uint32_t timeout_ms)
{
    uint32_t offset;
    int      i, j;

    __disable_irq();

    // ---- 路径 A：队非空，直接读取 ----
    if (q->count > 0) {
        offset = q->tail * q->msg_size;
        for (uint32_t k = 0; k < q->msg_size; k++) {
            ((uint8_t*)data)[k] = q->buffer[offset + k];
        }
        q->tail = (q->tail + 1) % q->queue_length;
        q->count--;

        /* 队中有了空位 → 若有发送者因队满阻塞，将其数据拷入缓冲区并唤醒它。
         * 发送者的数据指针保存在 TCB 的 blocked_queue_data 字段中。 */
        if (q->send_waiting_count > 0) {
            TASK_ADDR* sender = queue_wake_highest(q->send_waiting_tasks,
                                                    &q->send_waiting_count);
            if (sender && sender->blocked_queue_data) {
                // 将被唤醒发送者的数据拷入环形缓冲区
                offset = q->head * q->msg_size;
                for (uint32_t k = 0; k < q->msg_size; k++) {
                    q->buffer[offset + k] = ((const uint8_t*)sender->blocked_queue_data)[k];
                }
                q->head = (q->head + 1) % q->queue_length;
                q->count++;
                sender->blocked_queue_data = NULL;  // 数据已消费，清除指针
            }
        }
        __enable_irq();
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        return 0;
    }

    // 双重检查：从 count==0 判断到关中断窗口内，ISR 可能已发来数据
    if (q->count > 0) {
        offset = q->tail * q->msg_size;
        for (uint32_t k = 0; k < q->msg_size; k++) {
            ((uint8_t*)data)[k] = q->buffer[offset + k];
        }
        q->tail = (q->tail + 1) % q->queue_length;
        q->count--;

        if (q->send_waiting_count > 0) {
            TASK_ADDR* sender = queue_wake_highest(q->send_waiting_tasks,
                                                    &q->send_waiting_count);
            if (sender && sender->blocked_queue_data) {
                offset = q->head * q->msg_size;
                for (uint32_t k = 0; k < q->msg_size; k++) {
                    q->buffer[offset + k] = ((const uint8_t*)sender->blocked_queue_data)[k];
                }
                q->head = (q->head + 1) % q->queue_length;
                q->count++;
                sender->blocked_queue_data = NULL;
            }
        }
        __enable_irq();
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        return 0;
    }

    // ---- 路径 B：队空且 timeout_ms==0，不阻塞 ----
    if (timeout_ms == 0) {
        __enable_irq();
        return -1;
    }

    // ---- 路径 C：队空且 timeout_ms>0，阻塞等待 ----
    current_task->blocked_queue         = q;
    current_task->blocked_queue_is_send = 0;           // 0=接收阻塞
    q->recv_waiting_tasks[q->recv_waiting_count] = current_task;
    q->recv_waiting_count++;
    current_task->delay_ticks = timeout_ms;

    __enable_irq();

    // 让出 CPU，等发送者 queue_send 路径 A 写入数据并唤醒
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    // ---- 被唤醒后 ----
    if (current_task->blocked_queue != NULL) {
        // 超时：从接收等待列表移除自己
        __disable_irq();
        for (i = 0; i < q->recv_waiting_count; i++) {
            if (q->recv_waiting_tasks[i] == current_task) {
                for (j = i; j < q->recv_waiting_count - 1; j++) {
                    q->recv_waiting_tasks[j] = q->recv_waiting_tasks[j + 1];
                }
                q->recv_waiting_count--;
                break;
            }
        }
        current_task->blocked_queue  = NULL;
        current_task->blocked_queue_is_send = 0;
        __enable_irq();
        return -1;
    }

    // 被发送者唤醒 → 数据已在环形缓冲区中 → 重新走路径 A 读取
    __disable_irq();
    if (q->count > 0) {
        offset = q->tail * q->msg_size;
        for (uint32_t k = 0; k < q->msg_size; k++) {
            ((uint8_t*)data)[k] = q->buffer[offset + k];
        }
        q->tail = (q->tail + 1) % q->queue_length;
        q->count--;

        if (q->send_waiting_count > 0) {
            TASK_ADDR* sender = queue_wake_highest(q->send_waiting_tasks,
                                                    &q->send_waiting_count);
            if (sender && sender->blocked_queue_data) {
                offset = q->head * q->msg_size;
                for (uint32_t k = 0; k < q->msg_size; k++) {
                    q->buffer[offset + k] = ((const uint8_t*)sender->blocked_queue_data)[k];
                }
                q->head = (q->head + 1) % q->queue_length;
                q->count++;
                sender->blocked_queue_data = NULL;
            }
        }
    }
    __enable_irq();
    return 0;
}

void queue_send_from_isr(MSG_Q_ADDR* q, const void* data)
{
    uint32_t offset;

    __disable_irq();

    if (q->recv_waiting_count > 0) {
        // 有接收者在等 → 写入缓冲区并唤醒接收者
        offset = q->head * q->msg_size;
        for (uint32_t k = 0; k < q->msg_size; k++) {
            q->buffer[offset + k] = ((const uint8_t*)data)[k];
        }
        q->head = (q->head + 1) % q->queue_length;
        q->count++;

        TASK_ADDR* receiver = queue_wake_highest(q->recv_waiting_tasks,
                                                  &q->recv_waiting_count);
        (void)receiver;
        __enable_irq();
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        return;
    }

    if (q->count < q->queue_length) {
        // 队未满 → 写入环形缓冲区
        offset = q->head * q->msg_size;
        for (uint32_t k = 0; k < q->msg_size; k++) {
            q->buffer[offset + k] = ((const uint8_t*)data)[k];
        }
        q->head = (q->head + 1) % q->queue_length;
        q->count++;
    }
    // 队满且无接收者 → 静默丢弃（ISR 不能阻塞）

    __enable_irq();
}
