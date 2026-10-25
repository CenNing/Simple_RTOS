/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   主函数 — 初始化硬件、消息队列、任务，启动 RTOS
 * @note    演示内容：消息队列（生产者 → 消费者），串口打印 + LED 翻转验证
 ******************************************************************************
 */

#include "cos.h"
#include "uart.h"
#include "task.h"
#include "queue.h"

int main(void)
{
    // ---------- 硬件初始化 ----------
    USART1_Init();
    LED_Init();

    printf("Producer(T5, prio=2) --> Queue(32B x 8) --> Consumer(T4, prio=4)\r\n\r\n");

    // ---------- 初始化消息队列 ----------
    queue_init(&data_queue, queue_buffer, 32, 8);   // 32字节×8条消息

    // ---------- 初始化任务：入口函数 + 优先级 + 栈 + 栈大小 ----------
    task_init(&task_4,   task4,      4, task4_stack, 256);  // 消费者，高优先级
    task_init(&task_5,   task5,      2, task5_stack, 128);  // 生产者，中优先级
    task_init(&task_idle, idle_task, TASK_PRIO_IDLE, idle_stack, 64);

    TASK_ADDR* init_list[] = { &task_4, &task_5, &task_idle };
    for (int i = 0; i < TASK_COUNT; i++) task_list[i] = init_list[i];

    // ---------- 启动 RTOS 内核（永不返回） ----------
    task_start_scheduler();

    while (1) { }
}
