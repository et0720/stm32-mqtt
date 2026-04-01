#include "stm32f10x.h" /* 包含 STM32 标准外设库头文件。 */
#include "Delay.h" /* 包含延时函数声明。 */
#include "BH1750.h" /* 包含 BH1750 接口声明。 */

#define BH1750_I2C                  I2C1 /* 定义 BH1750 使用的 I2C 外设。 */
#define BH1750_ADDR                 0x46 /* 定义 BH1750 设备地址。 */
#define BH1750_CMD_POWER_ON         0x01 /* 定义 BH1750 上电命令。 */
#define BH1750_CMD_RESET            0x07 /* 定义 BH1750 数据寄存器复位命令。 */
#define BH1750_CMD_CONT_H_RES       0x10 /* 定义 BH1750 连续高分辨率测量命令。 */
#define BH1750_I2C_TIMEOUT          10000U /* 定义 I2C 访问超时计数值。 */

static ErrorStatus BH1750_WaitEvent(uint32_t Event) /* 等待指定 I2C 事件到来。 */
{
	uint32_t Timeout = BH1750_I2C_TIMEOUT; /* 初始化事件等待超时计数器。 */

	while (I2C_CheckEvent(BH1750_I2C, Event) == ERROR) /* 当目标事件没有发生时持续轮询。 */
	{
		if (Timeout-- == 0) /* 如果等待已经超时。 */
		{
			return ERROR; /* 返回错误状态表示事件等待失败。 */
		}
	}

	return SUCCESS; /* 返回成功状态表示事件已经发生。 */
}

static ErrorStatus BH1750_WaitBusIdle(void) /* 等待 I2C 总线空闲。 */
{
	uint32_t Timeout = BH1750_I2C_TIMEOUT; /* 初始化总线等待超时计数器。 */

	while (I2C_GetFlagStatus(BH1750_I2C, I2C_FLAG_BUSY) == SET) /* 当总线忙标志有效时持续等待。 */
	{
		if (Timeout-- == 0) /* 如果等待时间已经用完。 */
		{
			return ERROR; /* 返回错误状态表示总线没有及时空闲。 */
		}
	}

	return SUCCESS; /* 返回成功状态表示总线已经空闲。 */
}

static void BH1750_I2C_BusInit(void) /* 初始化 BH1750 共用的 I2C1 总线。 */
{
	static uint8_t Initialized = 0; /* 保存总线初始化状态。 */
	GPIO_InitTypeDef GPIO_InitStructure; /* 定义 GPIO 初始化结构体变量。 */
	I2C_InitTypeDef I2C_InitStructure; /* 定义 I2C 初始化结构体变量。 */

	if (Initialized != 0) /* 如果总线已经初始化过。 */
	{
		return; /* 直接返回避免重复初始化。 */
	}

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); /* 使能 GPIOB 端口时钟。 */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE); /* 使能 I2C1 外设时钟。 */

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7; /* 选择 PB6 和 PB7 作为 I2C 信号线。 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 设置引脚速度为 50MHz。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD; /* 设置引脚为复用开漏输出。 */
	GPIO_Init(GPIOB, &GPIO_InitStructure); /* 按配置初始化 GPIOB。 */

	I2C_DeInit(BH1750_I2C); /* 先复位 I2C1 外设。 */
	I2C_InitStructure.I2C_ClockSpeed = 100000; /* 设置 I2C 通信速率为 100kHz。 */
	I2C_InitStructure.I2C_Mode = I2C_Mode_I2C; /* 设置外设工作在 I2C 模式。 */
	I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2; /* 设置快速模式占空比参数。 */
	I2C_InitStructure.I2C_OwnAddress1 = 0x00; /* 设置主机自身地址为 0。 */
	I2C_InitStructure.I2C_Ack = I2C_Ack_Enable; /* 使能应答功能。 */
	I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit; /* 设置地址格式为 7 位。 */
	I2C_Init(BH1750_I2C, &I2C_InitStructure); /* 按参数初始化 I2C1。 */
	I2C_Cmd(BH1750_I2C, ENABLE); /* 使能 I2C1 开始工作。 */

	Initialized = 1; /* 标记总线初始化已经完成。 */
}

static uint8_t BH1750_Start(uint8_t Direction) /* 发送起始信号并完成地址阶段。 */
{
	if (BH1750_WaitBusIdle() == ERROR) /* 如果总线空闲等待失败。 */
	{
		return 1; /* 返回错误码 1 表示起始前总线异常。 */
	}

	I2C_GenerateSTART(BH1750_I2C, ENABLE); /* 发送 I2C 起始信号。 */
	if (BH1750_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == ERROR) /* 等待进入主机模式。 */
	{
		I2C_GenerateSTOP(BH1750_I2C, ENABLE); /* 失败时发送停止信号。 */
		return 1; /* 返回错误码 1。 */
	}

	I2C_Send7bitAddress(BH1750_I2C, BH1750_ADDR, Direction); /* 发送从机地址和读写方向。 */
	if (Direction == I2C_Direction_Transmitter) /* 如果当前方向是写操作。 */
	{
		if (BH1750_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR) /* 等待进入发送模式。 */
		{
			I2C_GenerateSTOP(BH1750_I2C, ENABLE); /* 失败时发送停止信号。 */
			return 1; /* 返回错误码 1。 */
		}
	}
	else /* 如果当前方向是读操作。 */
	{
		if (BH1750_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED) == ERROR) /* 等待进入接收模式。 */
		{
			I2C_GenerateSTOP(BH1750_I2C, ENABLE); /* 失败时发送停止信号。 */
			return 1; /* 返回错误码 1。 */
		}
	}

	return 0; /* 返回 0 表示地址阶段成功完成。 */
}

static uint8_t BH1750_WriteByte(uint8_t Byte) /* 向 BH1750 发送一个字节。 */
{
	I2C_SendData(BH1750_I2C, Byte); /* 把待发送字节写入数据寄存器。 */
	if (BH1750_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR) /* 等待字节发送完成。 */
	{
		I2C_GenerateSTOP(BH1750_I2C, ENABLE); /* 失败时发送停止信号。 */
		return 1; /* 返回错误码 1 表示发送失败。 */
	}

	return 0; /* 返回 0 表示发送成功。 */
}

static uint8_t BH1750_ReadByte(uint8_t LastByte) /* 从 BH1750 读取一个字节。 */
{
	uint32_t Timeout = BH1750_I2C_TIMEOUT; /* 初始化接收等待超时计数器。 */

	if (LastByte != 0) /* 如果这次读取的是最后一个字节。 */
	{
		I2C_AcknowledgeConfig(BH1750_I2C, DISABLE); /* 关闭应答准备结束接收。 */
		I2C_GenerateSTOP(BH1750_I2C, ENABLE); /* 提前发送停止信号结束本次传输。 */
	}

	while (I2C_GetFlagStatus(BH1750_I2C, I2C_FLAG_RXNE) == RESET) /* 当接收寄存器还没有数据时持续等待。 */
	{
		if (Timeout-- == 0) /* 如果等待超时。 */
		{
			I2C_GenerateSTOP(BH1750_I2C, ENABLE); /* 发送停止信号释放总线。 */
			break; /* 跳出等待循环。 */
		}
	}

	return I2C_ReceiveData(BH1750_I2C); /* 返回接收到的数据字节。 */
}

static uint8_t BH1750_WriteCommand(uint8_t Command) /* 向 BH1750 发送功能命令。 */
{
	if (BH1750_Start(I2C_Direction_Transmitter) != 0) /* 如果写方向起始失败。 */
	{
		return 1; /* 返回错误码 1。 */
	}

	if (BH1750_WriteByte(Command) != 0) /* 如果命令字节发送失败。 */
	{
		return 2; /* 返回错误码 2。 */
	}

	I2C_GenerateSTOP(BH1750_I2C, ENABLE); /* 命令发送后结束本次通信。 */
	return 0; /* 返回 0 表示命令发送成功。 */
}

void BH1750_Init(void) /* 初始化 BH1750 传感器。 */
{
	BH1750_I2C_BusInit(); /* 先初始化共用 I2C 总线。 */
	BH1750_WriteCommand(BH1750_CMD_POWER_ON); /* 发送上电命令。 */
	BH1750_WriteCommand(BH1750_CMD_RESET); /* 发送复位命令清空旧数据。 */
	BH1750_WriteCommand(BH1750_CMD_CONT_H_RES); /* 设置为连续高分辨率测量模式。 */
	Delay_ms(180); /* 等待首次测量完成。 */
}

uint8_t BH1750_ReadLux(uint16_t *Lux) /* 读取光照值并转换为 lux。 */
{
	uint16_t RawData; /* 保存传感器原始 16 位数据。 */
	uint32_t LuxValue; /* 保存换算后的光照值。 */
	uint8_t HighByte; /* 保存原始数据高字节。 */
	uint8_t LowByte; /* 保存原始数据低字节。 */

	if (Lux == 0) /* 如果输出指针为空。 */
	{
		return 1; /* 返回错误码 1 表示参数错误。 */
	}

	BH1750_I2C_BusInit(); /* 确保 I2C 总线已经初始化。 */
	I2C_AcknowledgeConfig(BH1750_I2C, ENABLE); /* 打开接收应答功能。 */

	if (BH1750_Start(I2C_Direction_Receiver) != 0) /* 如果读方向起始失败。 */
	{
		I2C_AcknowledgeConfig(BH1750_I2C, ENABLE); /* 恢复应答配置。 */
		return 2; /* 返回错误码 2。 */
	}

	HighByte = BH1750_ReadByte(0); /* 先读取原始数据高字节。 */
	LowByte = BH1750_ReadByte(1); /* 再读取原始数据低字节并结束接收。 */
	I2C_AcknowledgeConfig(BH1750_I2C, ENABLE); /* 恢复 I2C 应答配置。 */

	RawData = ((uint16_t)HighByte << 8) | LowByte; /* 把两个字节拼成 16 位原始值。 */
	LuxValue = ((uint32_t)RawData * 5U + 3U) / 6U; /* 按 1.2 的比例把原始值换算成 lux。 */
	if (LuxValue > 65535U) /* 如果换算结果超过 16 位范围。 */
	{
		LuxValue = 65535U; /* 把结果限制到 16 位最大值。 */
	}

	*Lux = (uint16_t)LuxValue; /* 把最终光照值写回调用者。 */
	return 0; /* 返回 0 表示读取成功。 */
}
