#include "stm32f10x.h"
#include "Delay.h"
#include "BH1750.h"

#define BH1750_I2C                  I2C1
#define BH1750_ADDR                 0x46
#define BH1750_CMD_POWER_ON         0x01
#define BH1750_CMD_RESET            0x07
#define BH1750_CMD_CONT_H_RES       0x10
#define BH1750_I2C_TIMEOUT          10000U

static ErrorStatus BH1750_WaitEvent(uint32_t Event)
{
	uint32_t Timeout = BH1750_I2C_TIMEOUT;

	while (I2C_CheckEvent(BH1750_I2C, Event) == ERROR)
	{
		if (Timeout-- == 0)
		{
			return ERROR;
		}
	}

	return SUCCESS;
}

static ErrorStatus BH1750_WaitBusIdle(void)
{
	uint32_t Timeout = BH1750_I2C_TIMEOUT;

	while (I2C_GetFlagStatus(BH1750_I2C, I2C_FLAG_BUSY) == SET)
	{
		if (Timeout-- == 0)
		{
			return ERROR;
		}
	}

	return SUCCESS;
}

static void BH1750_I2C_BusInit(void)
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

	I2C_DeInit(BH1750_I2C);
	I2C_InitStructure.I2C_ClockSpeed = 100000;
	I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
	I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
	I2C_InitStructure.I2C_OwnAddress1 = 0x00;
	I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
	I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
	I2C_Init(BH1750_I2C, &I2C_InitStructure);
	I2C_Cmd(BH1750_I2C, ENABLE);

	Initialized = 1;
}

static uint8_t BH1750_Start(uint8_t Direction)
{
	if (BH1750_WaitBusIdle() == ERROR)
	{
		return 1;
	}

	I2C_GenerateSTART(BH1750_I2C, ENABLE);
	if (BH1750_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == ERROR)
	{
		I2C_GenerateSTOP(BH1750_I2C, ENABLE);
		return 1;
	}

	I2C_Send7bitAddress(BH1750_I2C, BH1750_ADDR, Direction);
	if (Direction == I2C_Direction_Transmitter)
	{
		if (BH1750_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR)
		{
			I2C_GenerateSTOP(BH1750_I2C, ENABLE);
			return 1;
		}
	}
	else
	{
		if (BH1750_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED) == ERROR)
		{
			I2C_GenerateSTOP(BH1750_I2C, ENABLE);
			return 1;
		}
	}

	return 0;
}

static uint8_t BH1750_WriteByte(uint8_t Byte)
{
	I2C_SendData(BH1750_I2C, Byte);
	if (BH1750_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR)
	{
		I2C_GenerateSTOP(BH1750_I2C, ENABLE);
		return 1;
	}

	return 0;
}

static uint8_t BH1750_ReadByte(uint8_t LastByte)
{
	uint32_t Timeout = BH1750_I2C_TIMEOUT;

	if (LastByte != 0)
	{
		I2C_AcknowledgeConfig(BH1750_I2C, DISABLE);
		I2C_GenerateSTOP(BH1750_I2C, ENABLE);
	}

	while (I2C_GetFlagStatus(BH1750_I2C, I2C_FLAG_RXNE) == RESET)
	{
		if (Timeout-- == 0)
		{
			I2C_GenerateSTOP(BH1750_I2C, ENABLE);
			break;
		}
	}

	return I2C_ReceiveData(BH1750_I2C);
}

static uint8_t BH1750_WriteCommand(uint8_t Command)
{
	if (BH1750_Start(I2C_Direction_Transmitter) != 0)
	{
		return 1;
	}

	if (BH1750_WriteByte(Command) != 0)
	{
		return 2;
	}

	I2C_GenerateSTOP(BH1750_I2C, ENABLE);
	return 0;
}

void BH1750_Init(void)
{
	BH1750_I2C_BusInit();
	BH1750_WriteCommand(BH1750_CMD_POWER_ON);
	BH1750_WriteCommand(BH1750_CMD_RESET);
	BH1750_WriteCommand(BH1750_CMD_CONT_H_RES);
	Delay_ms(180);
}

uint8_t BH1750_ReadLux(uint16_t *Lux)
{
	uint16_t RawData;
	uint32_t LuxValue;
	uint8_t HighByte;
	uint8_t LowByte;

	if (Lux == 0)
	{
		return 1;
	}

	BH1750_I2C_BusInit();
	I2C_AcknowledgeConfig(BH1750_I2C, ENABLE);

	if (BH1750_Start(I2C_Direction_Receiver) != 0)
	{
		I2C_AcknowledgeConfig(BH1750_I2C, ENABLE);
		return 2;
	}

	HighByte = BH1750_ReadByte(0);
	LowByte = BH1750_ReadByte(1);
	I2C_AcknowledgeConfig(BH1750_I2C, ENABLE);

	RawData = ((uint16_t)HighByte << 8) | LowByte;
	LuxValue = ((uint32_t)RawData * 5U + 3U) / 6U;
	if (LuxValue > 65535U)
	{
		LuxValue = 65535U;
	}

	*Lux = (uint16_t)LuxValue;
	return 0;
}
