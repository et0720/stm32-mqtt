#include "serial.h"
#include <stdio.h>
#include "Delay.h"
#include "misc.h"
#include "app_config.h"

#if (APP_FREERTOS_ENABLE != 0U)
#include "FreeRTOSConfig.h"
#endif

static USART_TypeDef *Serial_PrintfUSART = USART1;

#define SERIAL_RX_BUFFER_SIZE 256U

typedef struct
{
	volatile uint8_t Buffer[SERIAL_RX_BUFFER_SIZE];
	volatile uint16_t Head;
	volatile uint16_t Tail;
} Serial_RxFifo;

static Serial_RxFifo Serial1_RxFifo;
static Serial_RxFifo Serial2_RxFifo;

static uint32_t Serial_GetAPBClockFromPrescaler(uint32_t ClockHz, uint32_t PrescalerBits)
{
	switch (PrescalerBits)
	{
		case RCC_CFGR_PPRE1_DIV2:
		case RCC_CFGR_PPRE2_DIV2:
			return ClockHz / 2U;

		case RCC_CFGR_PPRE1_DIV4:
		case RCC_CFGR_PPRE2_DIV4:
			return ClockHz / 4U;

		case RCC_CFGR_PPRE1_DIV8:
		case RCC_CFGR_PPRE2_DIV8:
			return ClockHz / 8U;

		case RCC_CFGR_PPRE1_DIV16:
		case RCC_CFGR_PPRE2_DIV16:
			return ClockHz / 16U;

		default:
			return ClockHz;
	}
}

static uint8_t Serial_HasErrorFlags(USART_TypeDef *USARTx)
{
	return ((USARTx->SR & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U) ? 1U : 0U;
}

static void Serial_EnableClock(USART_TypeDef *USARTx)
{
	if (USARTx == USART1)
	{
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
	}
	else if (USARTx == USART2)
	{
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
	}
}

static void Serial_EnableIRQ(USART_TypeDef *USARTx)
{
	NVIC_InitTypeDef NVIC_InitStructure;
	uint8_t PreemptionPriority = 1U;

	if ((USARTx != USART1) && (USARTx != USART2))
	{
		return;
	}

#if (APP_FREERTOS_ENABLE != 0U)
	/* USART IRQ handlers call xSemaphoreGiveFromISR(), so their priority must be
	   numerically equal to or larger than configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY. */
	PreemptionPriority = (uint8_t)configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
#endif

	NVIC_InitStructure.NVIC_IRQChannel = (USARTx == USART1) ? USART1_IRQn : USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = PreemptionPriority;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0U;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

static Serial_RxFifo *Serial_GetRxFifo(USART_TypeDef *USARTx)
{
	if (USARTx == USART1)
	{
		return &Serial1_RxFifo;
	}

	if (USARTx == USART2)
	{
		return &Serial2_RxFifo;
	}

	return 0;
}

static void Serial_ResetBufferedRx(USART_TypeDef *USARTx)
{
	Serial_RxFifo *RxFifo = Serial_GetRxFifo(USARTx);

	if (RxFifo == 0)
	{
		return;
	}

	__disable_irq();
	RxFifo->Head = 0U;
	RxFifo->Tail = 0U;
	__enable_irq();
}

static void Serial_BufferRxByte(USART_TypeDef *USARTx, uint8_t Data)
{
	uint16_t NextHead;
	Serial_RxFifo *RxFifo = Serial_GetRxFifo(USARTx);

	if (RxFifo == 0)
	{
		return;
	}

	NextHead = (uint16_t)((RxFifo->Head + 1U) % SERIAL_RX_BUFFER_SIZE);
	if (NextHead == RxFifo->Tail)
	{
		return;
	}

	RxFifo->Buffer[RxFifo->Head] = Data;
	RxFifo->Head = NextHead;
}

static uint8_t Serial_ReadBufferedByte(USART_TypeDef *USARTx, uint8_t *Data)
{
	Serial_RxFifo *RxFifo = Serial_GetRxFifo(USARTx);

	if (RxFifo == 0)
	{
		return 0U;
	}

	if (RxFifo->Head == RxFifo->Tail)
	{
		return 0U;
	}

	*Data = RxFifo->Buffer[RxFifo->Tail];
	RxFifo->Tail = (uint16_t)((RxFifo->Tail + 1U) % SERIAL_RX_BUFFER_SIZE);
	return 1U;
}

static void Serial_InitGPIO(USART_TypeDef *USARTx)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	if (USARTx == USART1)
	{
		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
		GPIO_Init(GPIOA, &GPIO_InitStructure);

		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
		GPIO_Init(GPIOA, &GPIO_InitStructure);
	}
	else if (USARTx == USART2)
	{
		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
		GPIO_Init(GPIOA, &GPIO_InitStructure);

		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
		GPIO_Init(GPIOA, &GPIO_InitStructure);
	}
}

static void Serial_InitGPIORxOnly(USART_TypeDef *USARTx)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	if (USARTx == USART1)
	{
		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
		GPIO_Init(GPIOA, &GPIO_InitStructure);

		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
		GPIO_Init(GPIOA, &GPIO_InitStructure);
	}
	else if (USARTx == USART2)
	{
		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
		GPIO_Init(GPIOA, &GPIO_InitStructure);

		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
		GPIO_Init(GPIOA, &GPIO_InitStructure);
	}
}

void Serial_Init(USART_TypeDef *USARTx, uint32_t BaudRate)
{
	USART_InitTypeDef USART_InitStructure;

	Serial_EnableClock(USARTx);
	USART_Cmd(USARTx, DISABLE);
	Serial_InitGPIO(USARTx);

	USART_InitStructure.USART_BaudRate = BaudRate;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_Init(USARTx, &USART_InitStructure);

	USART_Cmd(USARTx, ENABLE);
	Serial_ResetBufferedRx(USARTx);
	Serial_EnableIRQ(USARTx);
	if ((USARTx == USART1) || (USARTx == USART2))
	{
		USART_ITConfig(USARTx, USART_IT_RXNE, ENABLE);
	}
}

void Serial_InitRxOnly(USART_TypeDef *USARTx, uint32_t BaudRate)
{
	USART_InitTypeDef USART_InitStructure;

	Serial_EnableClock(USARTx);
	USART_Cmd(USARTx, DISABLE);
	Serial_InitGPIORxOnly(USARTx);

	USART_InitStructure.USART_BaudRate = BaudRate;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx;
	USART_Init(USARTx, &USART_InitStructure);

	USART_Cmd(USARTx, ENABLE);
	Serial_ResetBufferedRx(USARTx);
	Serial_EnableIRQ(USARTx);
	if ((USARTx == USART1) || (USARTx == USART2))
	{
		USART_ITConfig(USARTx, USART_IT_RXNE, ENABLE);
	}
}

void Serial_SetPrintfUSART(USART_TypeDef *USARTx)
{
	Serial_PrintfUSART = USARTx;
}

void Serial_SendCharBlocking(USART_TypeDef *USARTx, uint8_t Data)
{
	while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET)
	{
	}

	USART_SendData(USARTx, Data);
}

void Serial_SendBufferBlocking(USART_TypeDef *USARTx, const uint8_t *Buffer, uint16_t Length)
{
	uint16_t Index;

	for (Index = 0; Index < Length; Index++)
	{
		Serial_SendCharBlocking(USARTx, Buffer[Index]);
	}

	Serial_WaitTxComplete(USARTx);
}

void Serial_SendStringBlocking(USART_TypeDef *USARTx, const char *String)
{
	while (*String != '\0')
	{
		Serial_SendCharBlocking(USARTx, (uint8_t)*String);
		String++;
	}

	Serial_WaitTxComplete(USARTx);
}

void Serial_WaitTxComplete(USART_TypeDef *USARTx)
{
	while (USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET)
	{
	}
}

uint8_t Serial_ReadByteTimeout(USART_TypeDef *USARTx, uint8_t *Data, uint32_t TimeoutMs)
{
	while (TimeoutMs > 0U)
	{
		if (Serial_ReadBufferedByte(USARTx, Data) != 0U)
		{
			return 1U;
		}

		if (Serial_HasErrorFlags(USARTx) != 0U)
		{
			Serial_ClearErrors(USARTx);
		}

		if (USART_GetFlagStatus(USARTx, USART_FLAG_RXNE) != RESET)
		{
			*Data = (uint8_t)USART_ReceiveData(USARTx);
			return 1U;
		}

		Delay_ms(1U);
		TimeoutMs--;
	}

	return 0U;
}

uint8_t Serial_ReadByteNonBlocking(USART_TypeDef *USARTx, uint8_t *Data)
{
	if (Serial_ReadBufferedByte(USARTx, Data) != 0U)
	{
		return 1U;
	}

	if (Serial_HasErrorFlags(USARTx) != 0U)
	{
		Serial_ClearErrors(USARTx);
	}

	if (USART_GetFlagStatus(USARTx, USART_FLAG_RXNE) == RESET)
	{
		return 0U;
	}

	*Data = (uint8_t)USART_ReceiveData(USARTx);
	return 1U;
}

void Serial_ClearRxBuffer(USART_TypeDef *USARTx)
{
	uint8_t Data;

	Serial_ResetBufferedRx(USARTx);
	Serial_ClearErrors(USARTx);
	while (Serial_ReadByteNonBlocking(USARTx, &Data) != 0U)
	{
	}
}

void Serial_ClearErrors(USART_TypeDef *USARTx)
{
	volatile uint16_t Status;
	volatile uint16_t Data;

	Status = USARTx->SR;
	Data = USARTx->DR;
	(void)Status;
	(void)Data;
}

uint32_t Serial_GetPeripheralClock(USART_TypeDef *USARTx)
{
	SystemCoreClockUpdate();

	if (USARTx == USART1)
	{
		return Serial_GetAPBClockFromPrescaler(SystemCoreClock, RCC->CFGR & RCC_CFGR_PPRE2);
	}

	return Serial_GetAPBClockFromPrescaler(SystemCoreClock, RCC->CFGR & RCC_CFGR_PPRE1);
}

uint16_t Serial_GetBRR(USART_TypeDef *USARTx)
{
	return (uint16_t)USARTx->BRR;
}

void Serial_IRQHandler(USART_TypeDef *USARTx)
{
	uint16_t Status;
	uint8_t Data;

	Status = (uint16_t)USARTx->SR;

	if ((Status & USART_SR_RXNE) != 0U)
	{
		Data = (uint8_t)USARTx->DR;
		Serial_BufferRxByte(USARTx, Data);
		return;
	}

	if ((Status & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U)
	{
		Data = (uint8_t)USARTx->DR;
		(void)Data;
	}
}

int fputc(int ch, FILE *f)
{
	(void)f;

	if (Serial_PrintfUSART == 0)
	{
		return ch;
	}

	if (ch == '\n')
	{
		Serial_SendCharBlocking(Serial_PrintfUSART, '\r');
	}

	Serial_SendCharBlocking(Serial_PrintfUSART, (uint8_t)ch);
	return ch;
}
