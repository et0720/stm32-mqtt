#include "stm32f10x.h"
#include "Delay.h"
#include "OLED_Font.h"

#define OLED_I2C                   I2C1
#define OLED_ADDR                  0x78
#define OLED_I2C_TIMEOUT           10000U

static ErrorStatus OLED_WaitEvent(uint32_t Event)
{
	uint32_t Timeout = OLED_I2C_TIMEOUT;

	while (I2C_CheckEvent(OLED_I2C, Event) == ERROR)
	{
		if (Timeout-- == 0)
		{
			return ERROR;
		}
	}

	return SUCCESS;
}

static ErrorStatus OLED_WaitBusIdle(void)
{
	uint32_t Timeout = OLED_I2C_TIMEOUT;

	while (I2C_GetFlagStatus(OLED_I2C, I2C_FLAG_BUSY) == SET)
	{
		if (Timeout-- == 0)
		{
			return ERROR;
		}
	}

	return SUCCESS;
}

static void OLED_I2C_Init(void)
{
	static uint8_t Initialized = 0;
	GPIO_InitTypeDef GPIO_InitStructure;
	I2C_InitTypeDef I2C_InitStructure;

	if (Initialized != 0)
	{
		return;
	}

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	I2C_DeInit(OLED_I2C);
	I2C_InitStructure.I2C_ClockSpeed = 100000;
	I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
	I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
	I2C_InitStructure.I2C_OwnAddress1 = 0x00;
	I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
	I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
	I2C_Init(OLED_I2C, &I2C_InitStructure);
	I2C_Cmd(OLED_I2C, ENABLE);

	Initialized = 1;
}

static void OLED_WriteByte(uint8_t ControlByte, uint8_t DataByte)
{
	if (OLED_WaitBusIdle() == ERROR)
	{
		return;
	}

	I2C_GenerateSTART(OLED_I2C, ENABLE);
	if (OLED_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == ERROR)
	{
		I2C_GenerateSTOP(OLED_I2C, ENABLE);
		return;
	}

	I2C_Send7bitAddress(OLED_I2C, OLED_ADDR, I2C_Direction_Transmitter);
	if (OLED_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR)
	{
		I2C_GenerateSTOP(OLED_I2C, ENABLE);
		return;
	}

	I2C_SendData(OLED_I2C, ControlByte);
	if (OLED_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR)
	{
		I2C_GenerateSTOP(OLED_I2C, ENABLE);
		return;
	}

	I2C_SendData(OLED_I2C, DataByte);
	if (OLED_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR)
	{
		I2C_GenerateSTOP(OLED_I2C, ENABLE);
		return;
	}

	I2C_GenerateSTOP(OLED_I2C, ENABLE);
}

void OLED_WriteCommand(uint8_t Command)
{
	OLED_WriteByte(0x00, Command);
}

void OLED_WriteData(uint8_t Data)
{
	OLED_WriteByte(0x40, Data);
}

void OLED_SetCursor(uint8_t Y, uint8_t X)
{
	OLED_WriteCommand(0xB0 | Y);
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
	OLED_WriteCommand(0x00 | (X & 0x0F));
}

void OLED_Clear(void)
{
	uint8_t i;
	uint8_t j;

	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for (i = 0; i < 128; i++)
		{
			OLED_WriteData(0x00);
		}
	}
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
	uint8_t i;

	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i]);
	}

	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);
	}
}

void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;

	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;

	while (Y--)
	{
		Result *= X;
	}

	return Result;
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;

	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;

	if (Number >= 0)
	{
		OLED_ShowChar(Line, Column, '+');
		Number1 = Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, '-');
		Number1 = (uint32_t)(-Number);
	}

	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	uint8_t SingleNumber;

	for (i = 0; i < Length; i++)
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;

	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}

void OLED_Init(void)
{
	OLED_I2C_Init();
	Delay_ms(100);

	OLED_WriteCommand(0xAE);
	OLED_WriteCommand(0xD5);
	OLED_WriteCommand(0x80);
	OLED_WriteCommand(0xA8);
	OLED_WriteCommand(0x3F);
	OLED_WriteCommand(0xD3);
	OLED_WriteCommand(0x00);
	OLED_WriteCommand(0x40);
	OLED_WriteCommand(0xA1);
	OLED_WriteCommand(0xC8);
	OLED_WriteCommand(0xDA);
	OLED_WriteCommand(0x12);
	OLED_WriteCommand(0x81);
	OLED_WriteCommand(0xCF);
	OLED_WriteCommand(0xD9);
	OLED_WriteCommand(0xF1);
	OLED_WriteCommand(0xDB);
	OLED_WriteCommand(0x30);
	OLED_WriteCommand(0xA4);
	OLED_WriteCommand(0xA6);
	OLED_WriteCommand(0x8D);
	OLED_WriteCommand(0x14);
	OLED_WriteCommand(0xAF);

	OLED_Clear();
}
