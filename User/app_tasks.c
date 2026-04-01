#include "app_tasks.h" /* 包含任务上下文与对外接口定义。 */
#include "Delay.h" /* 包含延时接口，供裸机模式使用。 */
#include "OLED.h" /* 包含 OLED 显示接口。 */
#include "esp8266.h" /* 包含 ESP8266 AT 驱动接口。 */
#include "serial.h" /* 包含串口收发接口。 */
#include "MQTTX.H" /* 包含 WiFi 与 MQTT 业务模块接口。 */
#include "BH1750.h" /* 包含光照传感器驱动接口。 */
#include "Sth30.h" /* 包含温湿度传感器驱动接口。 */
#include "app_config.h" /* 包含工程级串口功能开关。 */
#include <stdio.h> /* 包含 printf/sprintf 声明。 */
#include <string.h> /* 包含 strlen/memcpy/memset 等字符串接口。 */

#if (APP_USART1_ENABLE != 0U)
static uint8_t AppTasks_IsAsciiSpace(uint8_t Data) /* 判断当前字节是否为空格或制表符。 */
{
	return ((Data == ' ') || (Data == '\t')) ? 1U : 0U; /* 是空白字符则返回 1。 */
}

static uint8_t AppTasks_IsAsciiLetterEqualIgnoreCase(uint8_t Data, char Letter) /* 忽略大小写比较一个 ASCII 字母。 */
{
	if ((Data >= 'a') && (Data <= 'z')) /* 如果当前字节是小写字母。 */
	{
		Data = (uint8_t)(Data - ('a' - 'A')); /* 先转换成大写再参与比较。 */
	}

	return (Data == (uint8_t)Letter) ? 1U : 0U; /* 返回比较结果。 */
}

static uint8_t AppTasks_IsATTestPrefix(const char *Command) /* 判断命令是否以 ATTEST: 前缀开头。 */
{
	const char *Prefix = "ATTEST:"; /* 定义透传命令固定前缀。 */
	uint8_t Index; /* 定义字符串遍历索引。 */

	for (Index = 0U; Prefix[Index] != '\0'; Index++) /* 逐字节比较前缀。 */
	{
		if (Command[Index] == '\0') /* 如果用户命令提前结束。 */
		{
			return 0U; /* 返回失败表示前缀不完整。 */
		}

		if (AppTasks_IsAsciiLetterEqualIgnoreCase((uint8_t)Command[Index], Prefix[Index]) == 0U) /* 如果某个字符不匹配。 */
		{
			return 0U; /* 返回失败表示不是 ATTEST 命令。 */
		}
	}

	return 1U; /* 返回成功表示检测到透传命令。 */
}

static void AppTasks_PrintResponseOrEmpty(const char *Response) /* 打印 ESP 响应文本，空响应则单独提示。 */
{
	if (Response[0] != '\0') /* 如果响应缓冲区里有内容。 */
	{
		printf("%s\r\n", Response); /* 先打印可读文本。 */
		ESP8266_PrintResponseHex(Response, ESP8266_GetLastResponseLength()); /* 再打印十六进制原始字节。 */
	}
	else
	{
		printf("(empty)\r\n"); /* 响应为空时打印占位提示。 */
	}
}

static uint8_t AppTasks_ExecuteATTestCommand(const char *Command, char *Response, uint16_t ResponseSize) /* 执行串口输入的 ATTEST 透传命令。 */
{
	char ForwardCommand[192]; /* 定义转发到 ESP 的命令缓冲区。 */
	uint16_t Length; /* 定义命令长度变量。 */

	while (AppTasks_IsAsciiSpace((uint8_t)*Command) != 0U) /* 跳过前导空白字符。 */
	{
		Command++; /* 把命令起始指针移动到有效字符。 */
	}

	if (AppTasks_IsATTestPrefix(Command) == 0U) /* 如果不是 ATTEST 前缀。 */
	{
		return 0U; /* 返回 0 表示让调用方继续按普通命令处理。 */
	}

	Command += 7; /* 跳过 "ATTEST:" 前缀本身。 */
	while (AppTasks_IsAsciiSpace((uint8_t)*Command) != 0U) /* 再跳过前缀后的空白字符。 */
	{
		Command++; /* 移动到真正的 AT 命令起始位置。 */
	}

	Length = (uint16_t)strlen(Command); /* 先计算剩余命令总长度。 */
	while ((Length > 0U) && /* 清理命令尾部的回车、换行和空白。 */
	       ((Command[Length - 1U] == '\r') ||
	        (Command[Length - 1U] == '\n') ||
	        (AppTasks_IsAsciiSpace((uint8_t)Command[Length - 1U]) != 0U)))
	{
		Length--; /* 把尾部无效字符从逻辑长度里裁掉。 */
	}

	if ((Length == 0U) || (Length >= (uint16_t)(sizeof(ForwardCommand) - 2U))) /* 如果命令为空或超出缓存。 */
	{
		printf("[ATTEST] empty command\r\n"); /* 打印命令为空提示。 */
		return 1U; /* 返回 1 表示该命令已经被消费。 */
	}

	memcpy(ForwardCommand, Command, Length); /* 把有效 AT 命令复制到转发缓冲区。 */
	ForwardCommand[Length++] = '\r'; /* 在命令末尾补充回车。 */
	ForwardCommand[Length++] = '\n'; /* 在命令末尾补充换行。 */
	ForwardCommand[Length] = '\0'; /* 补齐字符串结束符，便于日志打印。 */

	printf("[ATTEST] -> %s", ForwardCommand); /* 打印即将发给 ESP 的 AT 指令。 */
	if (ESP8266_SendCommandEx(ForwardCommand, NULL, Response, ResponseSize, 5000U) != 0U) /* 透传 AT 命令并等待最长 5 秒。 */
	{
		printf("[ATTEST] rx len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印收到的响应长度。 */
		AppTasks_PrintResponseOrEmpty(Response); /* 打印响应详情。 */
		return 1U; /* 返回 1 表示透传命令处理完成。 */
	}

	printf("[ATTEST] timeout len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印透传超时信息。 */
	AppTasks_PrintResponseOrEmpty(Response); /* 即使超时也打印已有残留数据。 */
	return 1U; /* 返回 1 表示透传命令处理完成。 */
}
#endif

static void AppTasks_PrintEspAsyncByte(uint8_t Data) /* 把 ESP 的异步字节按人类可读形式转发到调试串口。 */
{
#if (APP_USART1_ENABLE == 0U)
	(void)Data;
	return;
#else
	if ((Data == '\r') || (Data == '\n')) /* 如果当前字节是换行控制符。 */
	{
		Serial_SendCharBlocking(USART1, Data); /* 直接原样转发，保留文本换行结构。 */
		return; /* 结束当前字节处理。 */
	}

	if ((Data >= 32U) && (Data <= 126U)) /* 如果当前字节是可打印 ASCII 字符。 */
	{
		Serial_SendCharBlocking(USART1, Data); /* 直接转发到调试串口。 */
	}
	else
	{
		printf("<%02X>", (unsigned int)Data); /* 不可见字符改用十六进制包裹打印。 */
	}
#endif
}

void AppTasks_InitContext(AppTasks_Context *Context) /* 初始化任务上下文和底层传感器。 */
{
	memset(Context, 0, sizeof(*Context)); /* 先把整个上下文清零。 */
	STH30_Init(); /* 初始化温湿度传感器。 */
	BH1750_Init(); /* 初始化光照传感器。 */
	MQTTX_InitContext(&Context->MQTTContext); /* 初始化 MQTT 下行解析状态机。 */
}

void AppTasks_RunBootAutoConnect(AppTasks_Context *Context, const char *Ssid, const char *Password) /* 执行上电自动联网流程。 */
{
	strcpy(Context->Ssid, Ssid); /* 保存当前自动连接的 SSID。 */
	strcpy(Context->Password, Password); /* 保存当前自动连接的密码。 */
	Context->MqttReady = MQTTX_StartSession(Context->Ssid, /* 启动 WiFi + MQTT 整体建链流程。 */
	                                        Context->Password,
	                                        Context->Response,
	                                        sizeof(Context->Response),
	                                        &Context->LedState);
	Context->TelemetryElapsedMs = 0U; /* 建链完成后立即允许下一次遥测重新计时。 */
#if (APP_USART1_ENABLE != 0U)
	if (Context->MqttReady == 0U) /* 如果自动联网失败。 */
	{
		printf("[BOOT] auto connect failed, manual WIFI:ssid,password remains available\r\n"); /* 提示仍可手动发送 WIFI 命令。 */
	}
#endif
}

void AppTasks_CommandStep(AppTasks_Context *Context) /* 处理 PC 串口输入的命令。 */
{
#if (APP_USART1_ENABLE == 0U)
	(void)Context;
	return;
#else
	uint8_t PcData; /* 保存从 USART1 读到的一个字节。 */

	if (Serial_ReadByteNonBlocking(USART1, &PcData) != 0U) /* 如果串口当前收到了新字节。 */
	{
		Context->CommandIdleElapsedMs = 0U; /* 每收到一个字节都重置空闲提交计时器。 */
		if ((PcData == '\r') || (PcData == '\n')) /* 如果本次收到的是回车或换行。 */
		{
			if (Context->CommandLength != 0U) /* 只有当前缓存里确实有命令内容才提交。 */
			{
				Context->WiFiCommand[Context->CommandLength] = '\0'; /* 给命令缓冲区补结束符。 */
				Context->CommandLength = 0U; /* 立即清零长度，准备接收下一条命令。 */
				if (AppTasks_ExecuteATTestCommand(Context->WiFiCommand, Context->Response, sizeof(Context->Response)) != 0U) /* 优先识别并执行 ATTEST 透传命令。 */
				{
					return; /* 透传命令已经消费完成，不再继续走 WIFI 命令路径。 */
				}

				MQTTX_InitContext(&Context->MQTTContext); /* 新一轮联网前清空 MQTT 下行解析状态机。 */
				Context->MqttReady = MQTTX_ExecuteWiFiCommand(Context->WiFiCommand, /* 执行 WIFI:ssid,password 命令。 */
				                                             Context->Ssid,
				                                             sizeof(Context->Ssid),
				                                             Context->Password,
				                                             sizeof(Context->Password),
				                                             Context->Response,
				                                             sizeof(Context->Response),
				                                             &Context->LedState);
				Context->TelemetryElapsedMs = 0U; /* 手动联网后重新开始计算遥测周期。 */
			}
		}
		else if ((PcData == 0x08U) || (PcData == 0x7FU)) /* 如果用户按下退格键。 */
		{
			if (Context->CommandLength != 0U) /* 只有当前缓冲非空才允许删除。 */
			{
				Context->CommandLength--; /* 回退一个字符长度。 */
			}
		}
		else if (Context->CommandLength < (APP_TASKS_WIFI_COMMAND_SIZE - 1U)) /* 如果命令缓冲区还没满。 */
		{
			Context->WiFiCommand[Context->CommandLength++] = (char)PcData; /* 追加本次输入字符。 */
		}
	}
	else if ((Context->CommandLength != 0U) && (Context->CommandIdleElapsedMs >= APP_TASKS_WIFI_COMMAND_IDLE_MS)) /* 如果串口长时间空闲且缓存中已有内容。 */
	{
		Context->WiFiCommand[Context->CommandLength] = '\0'; /* 先补齐字符串结束符。 */
		Context->CommandLength = 0U; /* 清空当前长度计数。 */
		Context->CommandIdleElapsedMs = 0U; /* 清零空闲计时器。 */
		printf("[CMD] auto-submit on idle\r\n"); /* 打印空闲自动提交提示。 */
		if (AppTasks_ExecuteATTestCommand(Context->WiFiCommand, Context->Response, sizeof(Context->Response)) != 0U) /* 优先尝试按透传命令执行。 */
		{
			return; /* 如果已经作为透传命令处理，则直接返回。 */
		}

		MQTTX_InitContext(&Context->MQTTContext); /* 自动提交普通命令前也要重置 MQTT 解析状态机。 */
		Context->MqttReady = MQTTX_ExecuteWiFiCommand(Context->WiFiCommand, /* 按 WIFI:ssid,password 格式执行联网命令。 */
		                                             Context->Ssid,
		                                             sizeof(Context->Ssid),
		                                             Context->Password,
		                                             sizeof(Context->Password),
		                                             Context->Response,
		                                             sizeof(Context->Response),
		                                             &Context->LedState);
		Context->TelemetryElapsedMs = 0U; /* 联网动作完成后重置遥测计时。 */
	}
#endif
}

void AppTasks_LinkStep(AppTasks_Context *Context) /* 处理 ESP 异步串口数据与 MQTT 下行命令。 */
{
	uint8_t EspData; /* 保存从 USART2 读到的一个字节。 */

	if (Context->MqttReady == 0U) /* 如果 MQTT 还未就绪。 */
	{
		return; /* 直接返回，不处理异步数据。 */
	}

	while (Serial_ReadByteNonBlocking(USART2, &EspData) != 0U) /* 持续读空当前串口接收缓冲。 */
	{
		AppTasks_PrintEspAsyncByte(EspData); /* 先把 ESP 返回的原始字节打印出来便于调试。 */
		MQTTX_ConsumeAsyncByte(&Context->MQTTContext, /* 再交给 MQTT 下行状态机逐字节消费。 */
		                      EspData,
		                      &Context->LedState,
		                      Context->Response,
		                      sizeof(Context->Response));
	}
}

void AppTasks_TelemetryStep(AppTasks_Context *Context) /* 检查并执行周期遥测上报。 */
{
	if ((Context->MqttReady != 0U) && (Context->TelemetryElapsedMs >= MQTTX_TELEMETRY_INTERVAL_MS)) /* 只有 MQTT 就绪且到达周期才发数据。 */
	{
		MQTTX_PublishTelemetry(Context->LedState, Context->Response, sizeof(Context->Response)); /* 发布当前温湿度、光照和 LED 状态。 */
		Context->TelemetryElapsedMs = 0U; /* 发布成功与否都重新开始计时。 */
	}
}

void AppTasks_DisplayStep(AppTasks_Context *Context) /* 预留显示任务 step。 */
{
	(void)Context; /* 当前显示逻辑主要由业务模块事件驱动更新。 */
	/* 当前 OLED 更新仍由 WiFi/MQTT 业务流程里的事件驱动完成。 */
	/* 这里保留空壳，后续接成独立显示任务时只需要往此处填逻辑。 */
}

void AppTasks_Tick1ms(AppTasks_Context *Context) /* 推进所有基于软件计数的毫秒级定时器。 */
{
	if (Context->CommandIdleElapsedMs < APP_TASKS_WIFI_COMMAND_IDLE_MS) /* 如果命令空闲计时尚未饱和。 */
	{
		Context->CommandIdleElapsedMs++; /* 命令空闲计时加 1ms。 */
	}
	if (Context->TelemetryElapsedMs < MQTTX_TELEMETRY_INTERVAL_MS) /* 如果遥测计时尚未达到上限。 */
	{
		Context->TelemetryElapsedMs++; /* 遥测计时加 1ms。 */
	}
	if (Context->DisplayElapsedMs < 1000U) /* 如果显示刷新计时尚未达到占位上限。 */
	{
		Context->DisplayElapsedMs++; /* 显示计时加 1ms。 */
	}
}
