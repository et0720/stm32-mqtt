#include "stm32f10x.h" /* 包含 STM32 标准外设库头文件。 */
#include "Delay.h" /* 包含毫秒延时函数声明。 */
#include "OLED_Font.h" /* 包含 OLED 字模数据定义。 */

#define OLED_I2C                   I2C1 /* 定义 OLED 使用的 I2C 外设为 I2C1。 */
#define OLED_ADDR                  0x78 /* 定义 OLED 在总线上的写地址。 */
#define OLED_I2C_TIMEOUT           10000U /* 定义 I2C 等待超时计数值。 */

static ErrorStatus OLED_WaitEvent(uint32_t Event) /* 等待指定 I2C 事件完成。 */
{
	uint32_t Timeout = OLED_I2C_TIMEOUT; /* 把超时计数器初始化为默认值。 */

	while (I2C_CheckEvent(OLED_I2C, Event) == ERROR) /* 当目标事件还没有到来时持续等待。 */
	{
		if (Timeout-- == 0) /* 如果超时计数已经减到零。 */
		{
			return ERROR; /* 返回错误状态表示等待失败。 */
		}
	}

	return SUCCESS; /* 返回成功状态表示事件已经到来。 */
}

static ErrorStatus OLED_WaitBusIdle(void) /* 等待 I2C 总线恢复空闲。 */
{
	uint32_t Timeout = OLED_I2C_TIMEOUT; /* 把总线等待超时计数器初始化。 */

	while (I2C_GetFlagStatus(OLED_I2C, I2C_FLAG_BUSY) == SET) /* 当总线忙标志置位时一直等待。 */
	{
		if (Timeout-- == 0) /* 如果等待时间已经耗尽。 */
		{
			return ERROR; /* 返回错误状态表示总线长期忙碌。 */
		}
	}

	return SUCCESS; /* 返回成功状态表示总线已经空闲。 */
}

static void OLED_I2C_Init(void) /* 初始化 OLED 所在的 I2C1 总线。 */
{
	static uint8_t Initialized = 0; /* 记录总线是否已经初始化过。 */
	GPIO_InitTypeDef GPIO_InitStructure; /* 定义 GPIO 初始化结构体变量。 */
	I2C_InitTypeDef I2C_InitStructure; /* 定义 I2C 初始化结构体变量。 */

	if (Initialized != 0) /* 如果之前已经完成过初始化。 */
	{
		return; /* 直接返回避免重复配置同一总线。 */
	}

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); /* 打开 GPIOB 外设时钟。 */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE); /* 打开 I2C1 外设时钟。 */

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7; /* 选择 PB6 和 PB7 作为 I2C 引脚。 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 设置 GPIO 输出速度为 50MHz。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD; /* 设置 GPIO 为复用开漏模式。 */
	GPIO_Init(GPIOB, &GPIO_InitStructure); /* 按配置初始化 GPIOB 引脚。 */

	I2C_DeInit(OLED_I2C); /* 先复位 I2C1 外设到默认状态。 */
	I2C_InitStructure.I2C_ClockSpeed = 100000; /* 设置 I2C 时钟速度为 100kHz。 */
	I2C_InitStructure.I2C_Mode = I2C_Mode_I2C; /* 设置工作模式为标准 I2C 模式。 */
	I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2; /* 设置占空比参数为 2。 */
	I2C_InitStructure.I2C_OwnAddress1 = 0x00; /* 设置本机地址为 0。 */
	I2C_InitStructure.I2C_Ack = I2C_Ack_Enable; /* 使能 I2C 应答功能。 */
	I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit; /* 设置地址长度为 7 位。 */
	I2C_Init(OLED_I2C, &I2C_InitStructure); /* 按配置初始化 I2C1 外设。 */
	I2C_Cmd(OLED_I2C, ENABLE); /* 使能 I2C1 外设开始工作。 */

	Initialized = 1; /* 标记当前总线已经初始化完成。 */
}

static void OLED_WriteByte(uint8_t ControlByte, uint8_t DataByte) /* 发送一个控制字节和一个数据字节。 */
{
	if (OLED_WaitBusIdle() == ERROR) /* 如果等待总线空闲失败。 */
	{
		return; /* 直接返回放弃本次发送。 */
	}

	I2C_GenerateSTART(OLED_I2C, ENABLE); /* 产生 I2C 起始信号。 */
	if (OLED_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == ERROR) /* 等待主机模式选择事件。 */
	{
		I2C_GenerateSTOP(OLED_I2C, ENABLE); /* 失败时立即发送停止信号。 */
		return; /* 结束当前发送流程。 */
	}

	I2C_Send7bitAddress(OLED_I2C, OLED_ADDR, I2C_Direction_Transmitter); /* 发送 OLED 从机地址并进入发送模式。 */
	if (OLED_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR) /* 等待进入主发送模式。 */
	{
		I2C_GenerateSTOP(OLED_I2C, ENABLE); /* 失败时发送停止信号释放总线。 */
		return; /* 结束当前发送流程。 */
	}

	I2C_SendData(OLED_I2C, ControlByte); /* 先发送控制字节区分命令或数据。 */
	if (OLED_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR) /* 等待控制字节发送完成。 */
	{
		I2C_GenerateSTOP(OLED_I2C, ENABLE); /* 失败时发送停止信号。 */
		return; /* 结束当前发送流程。 */
	}

	I2C_SendData(OLED_I2C, DataByte); /* 再发送实际命令字节或数据字节。 */
	if (OLED_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR) /* 等待数据字节发送完成。 */
	{
		I2C_GenerateSTOP(OLED_I2C, ENABLE); /* 失败时发送停止信号。 */
		return; /* 结束当前发送流程。 */
	}

	I2C_GenerateSTOP(OLED_I2C, ENABLE); /* 正常完成后发送停止信号。 */
}

void OLED_WriteCommand(uint8_t Command) /* 向 OLED 写入一条命令。 */
{
	OLED_WriteByte(0x00, Command); /* 使用命令控制字节发送命令内容。 */
}

void OLED_WriteData(uint8_t Data) /* 向 OLED 写入一个显示数据字节。 */
{
	OLED_WriteByte(0x40, Data); /* 使用数据控制字节发送显示数据。 */
}

void OLED_SetCursor(uint8_t Y, uint8_t X) /* 设置 OLED 页地址和列地址。 */
{
	OLED_WriteCommand(0xB0 | Y); /* 设置页地址到指定的 Y 页。 */
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4)); /* 发送列地址高四位。 */
	OLED_WriteCommand(0x00 | (X & 0x0F)); /* 发送列地址低四位。 */
}

void OLED_Clear(void) /* 将整个 OLED 屏幕内容清零。 */
{
	uint8_t i; /* 定义列循环变量。 */
	uint8_t j; /* 定义页循环变量。 */

	for (j = 0; j < 8; j++) /* 依次处理 8 个显示页。 */
	{
		OLED_SetCursor(j, 0); /* 把光标移动到当前页首列。 */
		for (i = 0; i < 128; i++) /* 遍历当前页的 128 列。 */
		{
			OLED_WriteData(0x00); /* 向每一列写入零数据实现清屏。 */
		}
	}
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char) /* 在指定位置显示一个 8x16 字符。 */
{
	uint8_t i; /* 定义字模索引变量。 */

	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8); /* 把光标移到字符上半部分起始位置。 */
	for (i = 0; i < 8; i++) /* 依次发送字符上半部分的 8 列数据。 */
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i]); /* 发送字模上半部分当前列数据。 */
	}

	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8); /* 把光标移到字符下半部分起始位置。 */
	for (i = 0; i < 8; i++) /* 依次发送字符下半部分的 8 列数据。 */
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]); /* 发送字模下半部分当前列数据。 */
	}
}

void OLED_ShowString(uint8_t Line, uint8_t Column, char *String) /* 在指定位置连续显示字符串。 */
{
	uint8_t i; /* 定义字符串索引变量。 */

	for (i = 0; String[i] != '\0'; i++) /* 从头到尾遍历字符串直到结束符。 */
	{
		OLED_ShowChar(Line, Column + i, String[i]); /* 把当前字符显示到对应列位置。 */
	}
}

uint32_t OLED_Pow(uint32_t X, uint32_t Y) /* 计算 X 的 Y 次方。 */
{
	uint32_t Result = 1; /* 先把结果初始化为 1。 */

	while (Y--) /* 按指数次数循环累乘。 */
	{
		Result *= X; /* 每次把结果乘以基数 X。 */
	}

	return Result; /* 返回最终的乘方结果。 */
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length) /* 显示固定长度的无符号十进制数。 */
{
	uint8_t i; /* 定义位数循环变量。 */

	for (i = 0; i < Length; i++) /* 依次处理每一位数字。 */
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0'); /* 取出当前位并转换成字符显示。 */
	}
}

void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length) /* 显示固定长度的有符号十进制数。 */
{
	uint8_t i; /* 定义位数循环变量。 */
	uint32_t Number1; /* 保存绝对值后的数值。 */

	if (Number >= 0) /* 如果输入数字为非负数。 */
	{
		OLED_ShowChar(Line, Column, '+'); /* 先显示正号。 */
		Number1 = Number; /* 直接保存原始数值。 */
	}
	else /* 如果输入数字为负数。 */
	{
		OLED_ShowChar(Line, Column, '-'); /* 先显示负号。 */
		Number1 = (uint32_t)(-Number); /* 把负数转成正的绝对值。 */
	}

	for (i = 0; i < Length; i++) /* 依次处理符号后的每一位数字。 */
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0'); /* 取出当前位并显示为字符。 */
	}
}

void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length) /* 显示固定长度的十六进制数。 */
{
	uint8_t i; /* 定义位数循环变量。 */
	uint8_t SingleNumber; /* 保存当前处理的一位数值。 */

	for (i = 0; i < Length; i++) /* 依次处理每一个十六进制位。 */
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16; /* 取出当前十六进制位的值。 */
		if (SingleNumber < 10) /* 如果当前位在 0 到 9 之间。 */
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0'); /* 按数字字符形式显示当前位。 */
		}
		else /* 如果当前位在 A 到 F 之间。 */
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A'); /* 按大写字母字符形式显示当前位。 */
		}
	}
}

void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length) /* 显示固定长度的二进制数。 */
{
	uint8_t i; /* 定义位数循环变量。 */

	for (i = 0; i < Length; i++) /* 依次处理每一个二进制位。 */
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0'); /* 取出当前位并显示为字符 0 或 1。 */
	}
}

void OLED_Init(void) /* 初始化 OLED 模块并写入启动配置。 */
{
	OLED_I2C_Init(); /* 先初始化 OLED 使用的 I2C 总线。 */
	Delay_ms(100); /* 上电后延时等待屏幕稳定。 */

	OLED_WriteCommand(0xAE); /* 关闭显示输出。 */
	OLED_WriteCommand(0xD5); /* 设置显示时钟分频比命令。 */
	OLED_WriteCommand(0x80); /* 设置默认时钟分频参数。 */
	OLED_WriteCommand(0xA8); /* 设置复用比命令。 */
	OLED_WriteCommand(0x3F); /* 设置 64 路复用。 */
	OLED_WriteCommand(0xD3); /* 设置显示偏移命令。 */
	OLED_WriteCommand(0x00); /* 设置显示偏移为 0。 */
	OLED_WriteCommand(0x40); /* 设置显示起始行为 0。 */
	OLED_WriteCommand(0xA1); /* 设置段地址左右翻转。 */
	OLED_WriteCommand(0xC8); /* 设置公共端扫描方向反向。 */
	OLED_WriteCommand(0xDA); /* 设置 COM 引脚硬件配置命令。 */
	OLED_WriteCommand(0x12); /* 设置 COM 引脚配置参数。 */
	OLED_WriteCommand(0x81); /* 设置对比度命令。 */
	OLED_WriteCommand(0xCF); /* 设置对比度值。 */
	OLED_WriteCommand(0xD9); /* 设置预充电周期命令。 */
	OLED_WriteCommand(0xF1); /* 设置预充电周期参数。 */
	OLED_WriteCommand(0xDB); /* 设置 VCOMH 电平命令。 */
	OLED_WriteCommand(0x30); /* 设置 VCOMH 电平参数。 */
	OLED_WriteCommand(0xA4); /* 设置使用 RAM 内容显示。 */
	OLED_WriteCommand(0xA6); /* 设置普通显示模式。 */
	OLED_WriteCommand(0x8D); /* 设置电荷泵命令。 */
	OLED_WriteCommand(0x14); /* 使能电荷泵。 */
	OLED_WriteCommand(0xAF); /* 打开显示输出。 */

	OLED_Clear(); /* 初始化后清空整个显示区域。 */
}
