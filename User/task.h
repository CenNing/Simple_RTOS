/**
 ******************************************************************************
 * @file    User/task.h
 * @brief   应用任务 — 函数声明与全局实例 extern 声明
 ******************************************************************************
 */

#ifndef __TASK_H__
#define __TASK_H__

#include "cos.h"
#include "queue.h"

// ==================== 任务实例（extern） ====================
extern TASK_ADDR task_4;        // Task4 TCB：消息队列消费者（高优先级=4）
extern TASK_ADDR task_5;        // Task5 TCB：消息队列生产者（中优先级=2）
extern TASK_ADDR task_idle;     // Idle  TCB：空闲任务

// ==================== 消息队列实例（extern） ====================
extern MSG_Q_ADDR data_queue;   // 消息队列：生产者 → 消费者，每消息 32 字节

// ==================== 任务栈（各任务独立分配） ====================
extern int task4_stack[];       // task4 栈：消息队列接收 + 打印
extern int task5_stack[];       // task5 栈：消息队列发送 + 打印
extern int idle_stack[];        // idle 栈：空转等待

// ==================== 消息队列数据缓冲区（extern） ====================
extern uint8_t queue_buffer[];  // 队列缓冲区：8条消息 × 32字节 = 256字节

// ==================== 任务函数声明 ====================
void task4(void);               // 高优先级(4)：消息队列消费者，接收并打印消息
void task5(void);               // 中优先级(2)：消息队列生产者，周期性发送消息
void idle_task(void);           // 空闲任务，永远就绪，空转等待

#endif // __TASK_H__
