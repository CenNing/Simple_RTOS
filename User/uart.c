/**
 ******************************************************************************
 * @file    User/uart.c
 * @brief   串口与 LED 硬件抽象层 — 实现
 * @note    使用 STM32F10x 标准外设库
 *          safe_printf 通过 __disable_irq() 实现临界区保护
 ******************************************************************************
 */

#include "uart.h"
#include <stdarg.h>

// ==================== USART1 初始化 ====================

/**
 * @brief  初始化 USART1：PA9(TX) 推挽复用，PA10(RX) 浮空输入
 * @note   波特率 9600，8 数据位，1 停止位，无校验，无流控
 */
void USART1_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    // PA9 — TX：推挽复用输出
    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PA10 — RX：浮空输入
    GPIO_InitStruct.GPIO_Pin  = GPIO_Pin_10;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    // USART1 配置：9600-8-N-1
    USART_InitStruct.USART_BaudRate            = 9600;
    USART_InitStruct.USART_WordLength          = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits            = USART_StopBits_1;
    USART_InitStruct.USART_Parity              = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStruct);

    USART_Cmd(USART1, ENABLE);
}

// ==================== LED 初始化 ====================

/**
 * @brief  初始化 PC13 作为板载 LED 控制引脚
 * @note   初始状态：LED 熄灭（输出高电平）
 */
void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_13;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_SetBits(GPIOC, GPIO_Pin_13);       // 初始熄灭
}

// ==================== 串口底层发送 ====================

/**
 * @brief  阻塞发送一个字节到 USART1
 * @param  ch: 要发送的字节
 */
void USART_SendByte(uint8_t ch)
{
    while (!(USART1->SR & USART_SR_TXE));   // 等待发送数据寄存器为空
    USART1->DR = ch;
}

/**
 * @brief  重定向标准库 printf 到底层 USART_SendByte
 * @note   Keil MDK 使用 MicroLIB 时，重写 fputc 即可重定向 printf
 */
int fputc(int ch, FILE *f)
{
    USART_SendByte((uint8_t)ch);
    return ch;
}

// ==================== 线程安全的 printf ====================

/**
 * @brief  关中断保护的 printf（防止多任务抢占导致输出乱码）
 * @note   先格式化到局部缓冲区（栈上 128 字节），关中断后整体发送
 *         关中断期间 SysTick 不会触发，故任务不会切换
 * @warning 缓冲区固定 128 字节，超长字符串会导致栈溢出
 */
void safe_printf(const char *format, ...)
{
    char buf[128];
    va_list args;
    va_start(args, format);
    vsprintf(buf, format, args);            // vsprintf 在 microlib 中可用
    va_end(args);

    __disable_irq();                        // 进入临界区
    char *p = buf;
    while (*p) {
        while (!(USART1->SR & USART_SR_TXE));
        USART1->DR = *p++;
    }
    __enable_irq();                         // 退出临界区
}
