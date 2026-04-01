#ifndef __SERIAL_H
#define __SERIAL_H

#include "stm32f10x.h"
#include <stdint.h>

/* 简单的 USART 驱动：发送阻塞，接收走中断 + FIFO。 */
void Serial_Init(USART_TypeDef *USARTx, uint32_t BaudRate);
void Serial_InitRxOnly(USART_TypeDef *USARTx, uint32_t BaudRate);
void Serial_SetPrintfUSART(USART_TypeDef *USARTx);
void Serial_SendCharBlocking(USART_TypeDef *USARTx, uint8_t Data);
void Serial_SendBufferBlocking(USART_TypeDef *USARTx, const uint8_t *Buffer, uint16_t Length);
void Serial_SendStringBlocking(USART_TypeDef *USARTx, const char *String);
void Serial_WaitTxComplete(USART_TypeDef *USARTx);
uint8_t Serial_ReadByteTimeout(USART_TypeDef *USARTx, uint8_t *Data, uint32_t TimeoutMs);
uint8_t Serial_ReadByteNonBlocking(USART_TypeDef *USARTx, uint8_t *Data);
void Serial_ClearRxBuffer(USART_TypeDef *USARTx);
void Serial_ClearErrors(USART_TypeDef *USARTx);
uint32_t Serial_GetPeripheralClock(USART_TypeDef *USARTx);
uint16_t Serial_GetBRR(USART_TypeDef *USARTx);

/* 供具体 IRQHandler 调用的公共中断处理入口。 */
void Serial_IRQHandler(USART_TypeDef *USARTx);

#endif
