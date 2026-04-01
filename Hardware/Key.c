#include "stm32f10x.h" /* Device header */
#include "Delay.h"

/**
  * 函    数：Key_Init
  * 功    能：初始化 PB1 / PB11 按键输入
  * 返 回 值：无
  */
void Key_Init(void)
{
	/* 开启 GPIOB 时钟 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

	/* GPIO 初始化 */
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure); /* 将 PB1 和 PB11 初始化为上拉输入 */
}

/**
  * 函    数：Key_GetNum
  * 功    能：读取按键并返回对应键码
  * 返 回 值：按下按键的键码值，范围 0~2，0 代表没有按键按下
  * 注意事项：此函数是阻塞式操作，当按键按住不放时，函数会卡住，直到按键松手
  */
uint8_t Key_GetNum(void)
{
	uint8_t KeyNum = 0; /* 默认没有按键按下 */

	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0) /* PB1 为 0 代表按键 1 按下 */
	{
		Delay_ms(20); /* 延时消抖 */
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)
		{
		}
		Delay_ms(20); /* 延时消抖 */
		KeyNum = 1;
	}

	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0) /* PB11 为 0 代表按键 2 按下 */
	{
		Delay_ms(20); /* 延时消抖 */
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0)
		{
		}
		Delay_ms(20); /* 延时消抖 */
		KeyNum = 2;
	}

	return KeyNum;
}
