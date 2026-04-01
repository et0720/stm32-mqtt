#include "stm32f10x.h"
#include "app_config.h"

#if (APP_FREERTOS_ENABLE != 0U)
#include "FreeRTOS.h"
#include "task.h"
#endif

#ifndef DWT_BASE
typedef struct
{
	volatile uint32_t CTRL;
	volatile uint32_t CYCCNT;
	volatile uint32_t CPICNT;
	volatile uint32_t EXCCNT;
	volatile uint32_t SLEEPCNT;
	volatile uint32_t LSUCNT;
	volatile uint32_t FOLDCNT;
	volatile uint32_t PCSR;
} DelayDwt_TypeDef;

#define DWT_BASE                  (0xE0001000UL)
#define DWT                       ((DelayDwt_TypeDef *)DWT_BASE)
#define DWT_CTRL_CYCCNTENA_Msk    (1UL << 0)
#endif

static void Delay_EnableCycleCounter(void)
{
	if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
	{
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	}

	if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
	{
		DWT->CYCCNT = 0U;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	}
}

void Delay_us(uint32_t xus)
{
	uint32_t Cycles;
	uint32_t Start;

	if (xus == 0U)
	{
		return;
	}

	SystemCoreClockUpdate();
	Delay_EnableCycleCounter();

	Cycles = (SystemCoreClock / 1000000U) * xus;
	Start = DWT->CYCCNT;
	while ((DWT->CYCCNT - Start) < Cycles)
	{
	}
}

void Delay_ms(uint32_t xms)
{
#if (APP_FREERTOS_ENABLE != 0U)
	if ((xms != 0U) &&
	    ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) == 0U) &&
	    (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
	{
		vTaskDelay(pdMS_TO_TICKS(xms));
		return;
	}
#endif

	while (xms--)
	{
		Delay_us(1000U);
	}
}

void Delay_s(uint32_t xs)
{
	while (xs--)
	{
		Delay_ms(1000U);
	}
}
