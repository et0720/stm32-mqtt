#include "stm32f10x.h"
#include "Delay.h"
#include "Sth30.h"

#define STH30_I2C                   I2C1
#define STH30_ADDR                  0x88
#define STH30_I2C_TIMEOUT           10000U

static ErrorStatus STH30_WaitEvent(uint32_t Event)
{
	uint32_t Timeout = STH30_I2C_TIMEOUT;

	while (I2C_CheckEvent(STH30_I2C, Event) == ERROR)
	{
		if (Timeout-- == 0)
		{
			return ERROR;
		}
	}

	return SUCCESS;
}

static ErrorStatus STH30_WaitBusIdle(void)
{
	uint32_t Timeout = STH30_I2C_TIMEOUT;

	while (I2C_GetFlagStatus(STH30_I2C, I2C_FLAG_BUSY) == SET)
	{
		if (Timeout-- == 0)
		{
			return ERROR;
		}
	}

	return SUCCESS;
}

static void STH30_I2C_BusInit(void)
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

	I2C_DeInit(STH30_I2C);
	I2C_InitStructure.I2C_ClockSpeed = 100000;
	I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
	I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
	I2C_InitStructure.I2C_OwnAddress1 = 0x00;
	I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
	I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
	I2C_Init(STH30_I2C, &I2C_InitStructure);
	I2C_Cmd(STH30_I2C, ENABLE);

	Initialized = 1;
}

static uint8_t STH30_Start(uint8_t Direction)
{
	if (STH30_WaitBusIdle() == ERROR)
	{
		return 1;
	}

	I2C_GenerateSTART(STH30_I2C, ENABLE);
	if (STH30_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == ERROR)
	{
		I2C_GenerateSTOP(STH30_I2C, ENABLE);
		return 1;
	}

	I2C_Send7bitAddress(STH30_I2C, STH30_ADDR, Direction);
	if (Direction == I2C_Direction_Transmitter)
	{
		if (STH30_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR)
		{
			I2C_GenerateSTOP(STH30_I2C, ENABLE);
			return 1;
		}
	}
	else
	{
		if (STH30_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED) == ERROR)
		{
			I2C_GenerateSTOP(STH30_I2C, ENABLE);
			return 1;
		}
	}

	return 0;
}

static uint8_t STH30_WriteByte(uint8_t Byte)
{
	I2C_SendData(STH30_I2C, Byte);
	if (STH30_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR)
	{
		I2C_GenerateSTOP(STH30_I2C, ENABLE);
		return 1;
	}

	return 0;
}

static uint8_t STH30_ReadByte(uint8_t LastByte)
{
	uint32_t Timeout = STH30_I2C_TIMEOUT;

	if (LastByte != 0)
	{
		I2C_AcknowledgeConfig(STH30_I2C, DISABLE);
		I2C_GenerateSTOP(STH30_I2C, ENABLE);
	}

	while (I2C_GetFlagStatus(STH30_I2C, I2C_FLAG_RXNE) == RESET)
	{
		if (Timeout-- == 0)
		{
			I2C_GenerateSTOP(STH30_I2C, ENABLE);
			break;
		}
	}

	return I2C_ReceiveData(STH30_I2C);
}

static uint8_t STH30_CalculateCrc(const uint8_t *Buffer, uint8_t Length)
{
	uint8_t i;
	uint8_t j;
	uint8_t Crc = 0xFF;

	for (i = 0; i < Length; i++)
	{
		Crc ^= Buffer[i];
		for (j = 0; j < 8; j++)
		{
			if ((Crc & 0x80) != 0)
			{
				Crc = (uint8_t)((Crc << 1) ^ 0x31);
			}
			else
			{
				Crc <<= 1;
			}
		}
	}

	return Crc;
}

void STH30_Init(void)
{
	STH30_I2C_BusInit();
}

uint8_t STH30_ReadData(STH30_DataTypeDef *Data)
{
	uint8_t Buffer[6];
	uint16_t RawTemperature;
	uint16_t RawHumidity;
	uint8_t i;

	if (Data == 0)
	{
		return 1;
	}

	STH30_I2C_BusInit();
	I2C_AcknowledgeConfig(STH30_I2C, ENABLE);

	if (STH30_Start(I2C_Direction_Transmitter) != 0)
	{
		return 2;
	}
	if (STH30_WriteByte(0x24) != 0)
	{
		return 3;
	}
	if (STH30_WriteByte(0x00) != 0)
	{
		return 4;
	}
	I2C_GenerateSTOP(STH30_I2C, ENABLE);

	Delay_ms(20);

	if (STH30_Start(I2C_Direction_Receiver) != 0)
	{
		I2C_AcknowledgeConfig(STH30_I2C, ENABLE);
		return 5;
	}

	for (i = 0; i < 6; i++)
	{
		Buffer[i] = STH30_ReadByte(i == 5);
	}
	I2C_AcknowledgeConfig(STH30_I2C, ENABLE);

	if (STH30_CalculateCrc(Buffer, 2) != Buffer[2])
	{
		return 6;
	}
	if (STH30_CalculateCrc(&Buffer[3], 2) != Buffer[5])
	{
		return 7;
	}

	RawTemperature = ((uint16_t)Buffer[0] << 8) | Buffer[1];
	RawHumidity = ((uint16_t)Buffer[3] << 8) | Buffer[4];

	Data->Temperature = (int16_t)((1750 * (int32_t)RawTemperature) / 65535 - 450);
	Data->Humidity = (uint16_t)((1000U * (uint32_t)RawHumidity) / 65535U);

	return 0;
}
