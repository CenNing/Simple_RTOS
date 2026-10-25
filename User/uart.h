/**
 ******************************************************************************
 * @file    User/uart.h
 * @brief   串口与 LED 硬件抽象层 — API 声明
 ******************************************************************************
 */

#ifndef __UART_H__
#define __UART_H__

#include "stm32f10x.h"
#include <stdint.h>
#include <stdio.h>

// ==================== 硬件初始化 ====================
void USART1_Init(void);         // 初始化 USART1: PA9(TX) PA10(RX), 9600-8-N-1
void LED_Init(void);            // 初始化 PC13 LED（低电平点亮）

// ==================== 串口发送 ====================
void USART_SendByte(uint8_t ch);   // 阻塞发送单个字节
int  fputc(int ch, FILE *f);       // printf 重定向，重写标准库 fputc

// ==================== 线程安全 printf ====================
void safe_printf(const char *format, ...);  // 关中断保护的 printf，防止多任务输出乱码

#endif // __UART_H__
