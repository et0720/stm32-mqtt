#include "stm32f10x.h" /* 包含 STM32 标准外设库头文件。 */
#include "Delay.h" /* 包含延时函数声明。 */
#include "Sth30.h" /* 包含 STH30 接口声明。 */

#define STH30_I2C                   I2C1 /* 定义 STH30 使用的 I2C 外设。 */
#define STH30_ADDR                  0x88 /* 定义 STH30 设备地址。 */
#define STH30_I2C_TIMEOUT           10000U /* 定义 I2C 通信超时计数值。 */

static ErrorStatus STH30_WaitEvent(uint32_t Event) /* 等待指定 I2C 事件。 */
{
	uint32_t Timeout = STH30_I2C_TIMEOUT; /* 初始化事件等待超时计数器。 */

	while (I2C_CheckEvent(STH30_I2C, Event) == ERROR) /* 当事件还未满足时持续轮询。 */
	{
		if (Timeout-- == 0) /* 如果等待时间已经耗尽。 */
		{
			return ERROR; /* 返回错误状态表示等待失败。 */
		}
	}

	return SUCCESS; /* 返回成功状态表示事件已经发生。 */
}

static ErrorStatus STH30_WaitBusIdle(void) /* 等待 I2C 总线空闲。 */
{
	uint32_t Timeout = STH30_I2C_TIMEOUT; /* 初始化总线等待超时计数器。 */

	while (I2C_GetFlagStatus(STH30_I2C, I2C_FLAG_BUSY) == SET) /* 当总线忙标志有效时一直等待。 */
	{
		if (Timeout-- == 0) /* 如果等待超时。 */
		{
			return ERROR; /* 返回错误状态表示总线未空闲。 */
		}
	}

	return SUCCESS; /* 返回成功状态表示总线空闲。 */
}

static void STH30_I2C_BusInit(void) /* 初始化 STH30 共用的 I2C1 总线。 */
{
	static uint8_t Initialized = 0; /* 保存总线初始化状态。 */
	GPIO_InitTypeDef GPIO_InitStructure; /* 定义 GPIO 初始化结构体变量。 */
	I2C_InitTypeDef I2C_InitStructure; /* 定义 I2C 初始化结构体变量。 */

	if (Initialized != 0) /* 如果总线已经初始化过。 */
	{
		return; /* 直接返回避免重复配置。 */
	}

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); /* 使能 GPIOB 时钟。 */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE); /* 使能 I2C1 时钟。 */

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7; /* 选择 PB6 和 PB7 作为 I2C 引脚。 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 设置引脚速度为 50MHz。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD; /* 设置引脚为复用开漏模式。 */
	GPIO_Init(GPIOB, &GPIO_InitStructure); /* 应用 GPIO 配置到 GPIOB。 */

	I2C_DeInit(STH30_I2C); /* 先复位 I2C1 外设。 */
	I2C_InitStructure.I2C_ClockSpeed = 100000; /* 设置通信速率为 100kHz。 */
	I2C_InitStructure.I2C_Mode = I2C_Mode_I2C; /* 设置工作模式为 I2C。 */
	I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2; /* 设置占空比参数。 */
	I2C_InitStructure.I2C_OwnAddress1 = 0x00; /* 设置主机自身地址为 0。 */
	I2C_InitStructure.I2C_Ack = I2C_Ack_Enable; /* 使能接收应答功能。 */
	I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit; /* 设置使用 7 位地址格式。 */
	I2C_Init(STH30_I2C, &I2C_InitStructure); /* 按参数初始化 I2C1。 */
	I2C_Cmd(STH30_I2C, ENABLE); /* 使能 I2C1 外设。 */

	Initialized = 1; /* 标记总线初始化已经完成。 */
}

static uint8_t STH30_Start(uint8_t Direction) /* 发送起始信号并完成地址阶段。 */
{
	if (STH30_WaitBusIdle() == ERROR) /* 如果等待总线空闲失败。 */
	{
		return 1; /* 返回错误码 1。 */
	}

	I2C_GenerateSTART(STH30_I2C, ENABLE); /* 发送 I2C 起始信号。 */
	if (STH30_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == ERROR) /* 等待进入主机模式。 */
	{
		I2C_GenerateSTOP(STH30_I2C, ENABLE); /* 失败时发送停止信号。 */
		return 1; /* 返回错误码 1。 */
	}

	I2C_Send7bitAddress(STH30_I2C, STH30_ADDR, Direction); /* 发送从机地址和通信方向。 */
	if (Direction == I2C_Direction_Transmitter) /* 如果当前方向是发送命令。 */
	{
		if (STH30_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR) /* 等待进入发送模式。 */
		{
			I2C_GenerateSTOP(STH30_I2C, ENABLE); /* 失败时发送停止信号。 */
			return 1; /* 返回错误码 1。 */
		}
	}
	else /* 如果当前方向是接收数据。 */
	{
		if (STH30_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED) == ERROR) /* 等待进入接收模式。 */
		{
			I2C_GenerateSTOP(STH30_I2C, ENABLE); /* 失败时发送停止信号。 */
			return 1; /* 返回错误码 1。 */
		}
	}

	return 0; /* 返回 0 表示起始阶段成功。 */
}

static uint8_t STH30_WriteByte(uint8_t Byte) /* 向 STH30 发送一个字节。 */
{
	I2C_SendData(STH30_I2C, Byte); /* 把一个字节写入 I2C 数据寄存器。 */
	if (STH30_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR) /* 等待字节发送完成。 */
	{
		I2C_GenerateSTOP(STH30_I2C, ENABLE); /* 失败时发送停止信号。 */
		return 1; /* 返回错误码 1。 */
	}

	return 0; /* 返回 0 表示发送成功。 */
}

static uint8_t STH30_ReadByte(uint8_t LastByte) /* 从 STH30 接收一个字节。 */
{
	uint32_t Timeout = STH30_I2C_TIMEOUT; /* 初始化接收等待超时计数器。 */

	if (LastByte != 0) /* 如果当前读取的是最后一个字节。 */
	{
		I2C_AcknowledgeConfig(STH30_I2C, DISABLE); /* 关闭应答准备结束接收。 */
		I2C_GenerateSTOP(STH30_I2C, ENABLE); /* 发送停止信号结束本次读操作。 */
	}

	while (I2C_GetFlagStatus(STH30_I2C, I2C_FLAG_RXNE) == RESET) /* 当接收寄存器还没有数据时持续等待。 */
	{
		if (Timeout-- == 0) /* 如果等待超时。 */
		{
			I2C_GenerateSTOP(STH30_I2C, ENABLE); /* 发送停止信号释放总线。 */
			break; /* 跳出接收等待循环。 */
		}
	}

	return I2C_ReceiveData(STH30_I2C); /* 返回接收到的一个字节。 */
}

static uint8_t STH30_CalculateCrc(const uint8_t *Buffer, uint8_t Length) /* 计算 STH30 数据帧 CRC 校验值。 */
{
	uint8_t i; /* 定义数据索引变量。 */
	uint8_t j; /* 定义位索引变量。 */
	uint8_t Crc = 0xFF; /* 按协议把 CRC 初值设为 0xFF。 */

	for (i = 0; i < Length; i++) /* 依次处理缓冲区中的每个字节。 */
	{
		Crc ^= Buffer[i]; /* 先把当前字节与 CRC 累加值异或。 */
		for (j = 0; j < 8; j++) /* 再逐位执行 CRC 计算。 */
		{
			if ((Crc & 0x80) != 0) /* 如果当前最高位为 1。 */
			{
				Crc = (uint8_t)((Crc << 1) ^ 0x31); /* 左移后与多项式 0x31 异或。 */
			}
			else /* 如果当前最高位为 0。 */
			{
				Crc <<= 1; /* 仅执行左移操作。 */
			}
		}
	}

	return Crc; /* 返回最终 CRC 结果。 */
}

void STH30_Init(void) /* 初始化 STH30 传感器接口。 */
{
	STH30_I2C_BusInit(); /* 初始化 STH30 所使用的 I2C 总线。 */
}

uint8_t STH30_ReadData(STH30_DataTypeDef *Data) /* 读取一次温湿度数据。 */
{
	uint8_t Buffer[6]; /* 保存传感器返回的 6 个数据字节。 */
	uint16_t RawTemperature; /* 保存原始温度值。 */
	uint16_t RawHumidity; /* 保存原始湿度值。 */
	uint8_t i; /* 定义循环变量。 */

	if (Data == 0) /* 如果输出结构体指针为空。 */
	{
		return 1; /* 返回错误码 1 表示参数错误。 */
	}

	STH30_I2C_BusInit(); /* 确保 I2C 总线已经初始化。 */
	I2C_AcknowledgeConfig(STH30_I2C, ENABLE); /* 打开接收应答功能。 */

	if (STH30_Start(I2C_Direction_Transmitter) != 0) /* 如果发送测量命令的起始阶段失败。 */
	{
		return 2; /* 返回错误码 2。 */
	}
	if (STH30_WriteByte(0x24) != 0) /* 发送高重复度测量命令高字节。 */
	{
		return 3; /* 返回错误码 3。 */
	}
	if (STH30_WriteByte(0x00) != 0) /* 发送高重复度测量命令低字节。 */
	{
		return 4; /* 返回错误码 4。 */
	}
	I2C_GenerateSTOP(STH30_I2C, ENABLE); /* 命令发送完成后结束本次写操作。 */

	Delay_ms(20); /* 等待传感器完成一次测量转换。 */

	if (STH30_Start(I2C_Direction_Receiver) != 0) /* 如果读数据阶段起始失败。 */
	{
		I2C_AcknowledgeConfig(STH30_I2C, ENABLE); /* 恢复应答配置。 */
		return 5; /* 返回错误码 5。 */
	}

	for (i = 0; i < 6; i++) /* 依次读取返回的 6 个字节。 */
	{
		Buffer[i] = STH30_ReadByte(i == 5); /* 在最后一个字节时关闭应答并结束读取。 */
	}
	I2C_AcknowledgeConfig(STH30_I2C, ENABLE); /* 恢复 I2C 应答功能。 */

	if (STH30_CalculateCrc(Buffer, 2) != Buffer[2]) /* 校验温度原始值对应的 CRC。 */
	{
		return 6; /* 返回错误码 6 表示温度 CRC 错误。 */
	}
	if (STH30_CalculateCrc(&Buffer[3], 2) != Buffer[5]) /* 校验湿度原始值对应的 CRC。 */
	{
		return 7; /* 返回错误码 7 表示湿度 CRC 错误。 */
	}

	RawTemperature = ((uint16_t)Buffer[0] << 8) | Buffer[1]; /* 组合出 16 位原始温度值。 */
	RawHumidity = ((uint16_t)Buffer[3] << 8) | Buffer[4]; /* 组合出 16 位原始湿度值。 */

	Data->Temperature = (int16_t)((1750 * (int32_t)RawTemperature) / 65535 - 450); /* 把原始温度转换成放大 10 倍的摄氏值。 */
	Data->Humidity = (uint16_t)((1000U * (uint32_t)RawHumidity) / 65535U); /* 把原始湿度转换成放大 10 倍的相对湿度值。 */

	return 0; /* 返回 0 表示读取成功。 */
}
