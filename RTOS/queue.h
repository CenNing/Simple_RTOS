/**
 ******************************************************************************
 * @file    RTOS/queue.h
 * @brief   COS RTOS 消息队列 — 控制块定义与 API 声明（依赖 cos.h）
 ******************************************************************************
 */

#ifndef __QUEUE_H__
#define __QUEUE_H__

#include "cos.h"

// ==================== 消息队列控制块 MSG_Q ====================

/**
 * @brief  消息队列控制块（FreeRTOS 风格，数据按值拷贝）
 * @note   底层为环形缓冲区（circular buffer），head 写入、tail 读取。
 *         支持多任务等待发送（队满时阻塞）和多任务等待接收（队空时阻塞）。
 *         等待列表按优先级排序取最高，同优先级 FIFO。
 *
 *         与 FreeRTOS xQueueCreate / xQueueSend / xQueueReceive 等价。
 *
 *         优化：直接传递（Direct Transfer）
 *         当有接收者正在等待且队列为空时，发送者直接将数据拷给接收者，
 *         跳过环形缓冲区，减少一次 memcpy。同理，当有发送者阻塞等待且
 *         队列为满时，接收者直接从发送者的数据区拷走数据。
 */
struct _MSG_Q_ADDR {
    // ---- 数据区 ----
    uint8_t*   buffer;                              // 环形缓冲区（外部分配）
    uint32_t   msg_size;                            // 每条消息的大小（字节）
    uint32_t   queue_length;                        // 队列最大容量（消息条数）
    uint32_t   count;                               // 当前队列中缓存的条数
    uint32_t   head;                                // 写索引（下一写入位置）
    uint32_t   tail;                                // 读索引（下一读出位置）

    // ---- 接收等待列表（队空时阻塞于此）----
    TASK_ADDR* recv_waiting_tasks[TASK_COUNT];             // 等待接收的任务列表
    uint8_t    recv_waiting_count;                   // 等待接收的任务数

    // ---- 发送等待列表（队满时阻塞于此）----
    TASK_ADDR* send_waiting_tasks[TASK_COUNT];             // 等待发送的任务列表
    uint8_t    send_waiting_count;                   // 等待发送的任务数
};

// ==================== 消息队列 API ====================

#define QUEUE_WAIT_FOREVER 0xFFFFFFFF // 队列无限等待

void queue_init       (MSG_Q_ADDR* q, void* buffer, uint32_t msg_size, uint32_t queue_length);
int  queue_send       (MSG_Q_ADDR* q, const void* data, uint32_t timeout_ms);
int  queue_receive    (MSG_Q_ADDR* q, void* data, uint32_t timeout_ms);
void queue_send_from_isr(MSG_Q_ADDR* q, const void* data);

#endif // __QUEUE_H__
