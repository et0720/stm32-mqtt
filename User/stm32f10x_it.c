/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.c
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   Main Interrupt Service Routines.
  ******************************************************************************
  */

#include "stm32f10x_it.h"
#include "app_config.h"
#include "app_freertos.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
	while (1)
	{
	}
}

void MemManage_Handler(void)
{
	while (1)
	{
	}
}

void BusFault_Handler(void)
{
	while (1)
	{
	}
}

void UsageFault_Handler(void)
{
	while (1)
	{
	}
}

#if (APP_FREERTOS_ENABLE == 0U)
void SVC_Handler(void)
{
}
#endif

void DebugMon_Handler(void)
{
}

#if (APP_FREERTOS_ENABLE == 0U)
void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
}
#endif

void USART1_IRQHandler(void)
{
	Serial_IRQHandler(USART1);
	portYIELD_FROM_ISR(AppRTOS_GiveRxSemaphoreFromISR(USART1));
}

void USART2_IRQHandler(void)
{
	Serial_IRQHandler(USART2);
	portYIELD_FROM_ISR(AppRTOS_GiveRxSemaphoreFromISR(USART2));
}
