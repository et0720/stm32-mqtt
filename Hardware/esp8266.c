#include "esp8266.h" /* 包含 ESP8266 驱动头文件。 */
#include <stdio.h> /* 包含 printf/sprintf 声明。 */
#include <string.h> /* 包含字符串处理接口。 */
#include "stm32f10x.h" /* 包含 STM32 设备定义。 */
#include "Delay.h" /* 包含阻塞延时接口。 */
#include "serial.h" /* 包含底层串口驱动接口。 */

static uint32_t ESP8266_BaudRate = 115200U; /* 保存当前驱动层认为的 ESP 波特率。 */
static uint16_t ESP8266_LastResponseLength = 0U; /* 保存最近一次 AT 响应的字节长度。 */

#define ESP8266_POWER_ON_DELAY_MS        2000U /* 定义 ESP 上电后首次探测前的等待时间。 */
#define ESP8266_PROBE_ATTEMPTS           3U /* 定义每个波特率下的探测次数。 */
#define ESP8266_DEFAULT_TIMEOUT_MS       1000U /* 定义普通 AT 命令默认超时。 */
#define ESP8266_SHORT_TIMEOUT_MS         2000U /* 定义短指令超时。 */
#define ESP8266_WIFI_JOIN_TIMEOUT_MS     20000U /* 定义 WiFi 连接指令超时。 */
#define ESP8266_WIFI_QUERY_TIMEOUT_MS    3000U /* 定义 WiFi 查询指令超时。 */
#define ESP8266_MQTT_TIMEOUT_MS          5000U /* 定义 MQTT 普通指令超时。 */
#define ESP8266_MQTT_CONN_TIMEOUT_MS     20000U /* 定义 MQTT 建链指令超时。 */

static const uint32_t ESP8266_BaudCandidates[] = {115200U, 9600U, 57600U, 38400U, 74880U, 19200U}; /* 定义启动探测时尝试的候选波特率。 */

static void ESP8266_PrintBytes(const char *Prefix, const uint8_t *Buffer, uint16_t Length) /* 打印一段发送或接收数据的十六进制字节。 */
{
	uint16_t Index; /* 定义遍历缓冲区的索引。 */

	printf("%s (%u):", Prefix, Length); /* 先打印前缀和数据长度。 */
	for (Index = 0; Index < Length; Index++) /* 逐字节打印十六进制值。 */
	{
		printf(" %02X", (unsigned int)Buffer[Index]); /* 打印当前字节。 */
	}
	printf("\r\n"); /* 当前日志行结束。 */
}

static void ESP8266_AppendByte(char *Buffer, uint16_t Size, uint16_t *Length, uint8_t Data) /* 向文本响应缓冲区安全追加一个字节。 */
{
	if (*Length < (uint16_t)(Size - 1U)) /* 只有缓冲区还有空间时才写入。 */
	{
		Buffer[*Length] = (char)Data; /* 把当前字节写入缓冲区。 */
		(*Length)++; /* 更新当前长度。 */
		Buffer[*Length] = '\0'; /* 始终维持字符串结束符。 */
	}
}

static uint8_t ESP8266_EscapeQuotedString(char *Destination, uint16_t DestinationSize, const char *Source) /* 转义 AT 双引号字符串里的特殊字符。 */
{
	uint16_t Index = 0U; /* 保存输出写入位置。 */

	if ((Destination == 0) || (Source == 0) || (DestinationSize == 0U)) /* 如果输入输出指针无效。 */
	{
		return 0U; /* 返回失败。 */
	}

	while (*Source != '\0') /* 逐字符扫描源字符串。 */
	{
		if ((*Source == '"') || (*Source == '\\') || (*Source == ',')) /* 如果遇到引号、反斜杠或逗号。 */
		{
			if ((Index + 2U) >= DestinationSize) /* 如果补转义后缓冲区会溢出。 */
			{
				return 0U; /* 返回失败。 */
			}

			Destination[Index++] = '\\'; /* 先补一个反斜杠。 */
		}
		else if ((Index + 1U) >= DestinationSize) /* 普通字符也要预留结束符空间。 */
		{
			return 0U; /* 返回失败。 */
		}

		Destination[Index++] = *Source; /* 写入原始字符本体。 */
		Source++; /* 继续处理下一个字符。 */
	}

	Destination[Index] = '\0'; /* 补齐结束符。 */
	return 1U; /* 返回成功。 */
}

static uint8_t ESP8266_SendCommandInternal(const char *Command, /* 发送 AT 命令的内部统一实现。 */
                                          const char *Expected,
                                          char *Response,
                                          uint16_t Size,
                                          uint32_t TimeoutMs,
                                          uint8_t PrintCommand)
{
	uint16_t CommandLength; /* 保存命令总长度。 */
	uint16_t ResponseLength = 0U; /* 保存响应缓冲区当前写入长度。 */
	uint16_t Index; /* 保存发送命令时的字节索引。 */
	uint32_t ElapsedMs = 0U; /* 保存等待响应的经过时间。 */
	uint8_t Data; /* 保存串口收到的一个字节。 */

	if (Size == 0U) /* 如果调用方没有给出有效响应缓冲区。 */
	{
		return 0U; /* 直接返回失败。 */
	}

	Serial_ClearRxBuffer(USART2); /* 发送新命令前先清空接收残留。 */
	Response[0] = '\0'; /* 清空响应字符串。 */
	CommandLength = (uint16_t)strlen(Command); /* 计算命令长度。 */
	if (PrintCommand != 0U) /* 如果允许打印发送日志。 */
	{
		ESP8266_PrintBytes("ESP8266 tx bytes", (const uint8_t *)Command, CommandLength); /* 打印发送的字节流。 */
	}

	for (Index = 0; Index < CommandLength; Index++) /* 逐字节发送整条命令。 */
	{
		Serial_SendCharBlocking(USART2, (uint8_t)Command[Index]); /* 发送当前字节。 */
		while (Serial_ReadByteNonBlocking(USART2, &Data) != 0U) /* 在发送间隙顺手捞走已经返回的字节。 */
		{
			ESP8266_AppendByte(Response, Size, &ResponseLength, Data); /* 追加到响应缓冲区里。 */
		}
	}

	Serial_WaitTxComplete(USART2); /* 等待最后一个字节真正发完。 */

	while (ElapsedMs < TimeoutMs) /* 继续等待响应直到超时。 */
	{
		while (Serial_ReadByteNonBlocking(USART2, &Data) != 0U) /* 只要串口里还有字节就持续读取。 */
		{
			ESP8266_AppendByte(Response, Size, &ResponseLength, Data); /* 把响应字节追加到缓冲区。 */
			if ((Expected != NULL) && (strstr(Response, Expected) != NULL)) /* 如果已经匹配到期望关键字。 */
			{
				ESP8266_LastResponseLength = ResponseLength; /* 记录本次响应长度。 */
				return 1U; /* 返回成功。 */
			}
		}

		Delay_ms(1U); /* 每轮等待 1ms。 */
		ElapsedMs++; /* 递增等待时间。 */
	}

	ESP8266_LastResponseLength = ResponseLength; /* 超时前记录最终收到的长度。 */
	if (Expected == NULL) /* 如果调用方只要求收到任意响应即可。 */
	{
		return (ResponseLength != 0U) ? 1U : 0U; /* 按是否收到字节决定成功与否。 */
	}

	return (strstr(Response, Expected) != NULL) ? 1U : 0U; /* 否则按是否包含期望关键字决定成功。 */
}

uint8_t ESP8266_SendCommand(const char *Command, const char *Expected, char *Response, uint16_t Size)
{
	return ESP8266_SendCommandInternal(Command,
	                                  Expected,
	                                  Response,
	                                  Size,
	                                  ESP8266_DEFAULT_TIMEOUT_MS,
	                                  1U);
}

uint8_t ESP8266_SendCommandEx(const char *Command, const char *Expected, char *Response, uint16_t Size, uint32_t TimeoutMs)
{
	return ESP8266_SendCommandInternal(Command, Expected, Response, Size, TimeoutMs, 1U);
}

static uint8_t ESP8266_ProbeBaudRate(uint32_t BaudRate, char *Response, uint16_t Size) /* 用指定波特率探测 ESP 是否在线。 */
{
	uint8_t Attempt;

	printf("ESP probe baud=%lu\r\n", BaudRate);
	ESP8266_Init(BaudRate);
	printf("  USART2 PCLK=%lu BRR=0x%04X\r\n", Serial_GetPeripheralClock(USART2), Serial_GetBRR(USART2));
	Delay_ms(300);
	Serial_ClearRxBuffer(USART2);

	for (Attempt = 0; Attempt < ESP8266_PROBE_ATTEMPTS; Attempt++)
	{
		printf("  try %u\r\n", (unsigned int)(Attempt + 1U));
		if (ESP8266_SendCommand("AT\r\n", "OK", Response, Size) != 0U)
		{
			/* AT succeeds is enough to prove PA2/PA3 UART link is alive. */
			printf("  AT OK, rx_len=%u\r\n", ESP8266_LastResponseLength);
			ESP8266_SendCommand("ATE0\r\n", "OK", Response, Size);
			ESP8266_SendCommand("AT+GMR\r\n", "OK", Response, Size);
			return 1;
		}
		
		printf("  AT fail, rx_len=%u\r\n", ESP8266_LastResponseLength);
		if (ESP8266_LastResponseLength != 0U)
		{
			printf("  resp=%s\r\n", Response);
			ESP8266_PrintResponseHex(Response, ESP8266_LastResponseLength);
		}

		Delay_ms(300);
	}

	return 0;
}

static uint8_t ESP8266_SetBaudRate(uint32_t TargetBaudRate, char *Response, uint16_t Size) /* 下发改波特率命令。 */
{
	char Command[40];

	sprintf(Command, "AT+UART_DEF=%lu,8,1,0,0\r\n", TargetBaudRate);
	if (ESP8266_SendCommand(Command, "OK", Response, Size) != 0U)
	{
		return 1;
	}

	sprintf(Command, "AT+CIOBAUD=%lu\r\n", TargetBaudRate);
	return ESP8266_SendCommand(Command, "OK", Response, Size);
}

void ESP8266_Init(uint32_t BaudRate) /* 初始化 ESP 对应串口并记录当前波特率。 */
{
	Serial_Init(USART2, BaudRate);
	ESP8266_BaudRate = BaudRate;
}

uint8_t ESP8266_RunStartupTest(char *Response, uint16_t Size) /* 上电后依次探测常见波特率。 */
{
	uint8_t Index;

	Delay_ms(ESP8266_POWER_ON_DELAY_MS);
	Response[0] = '\0';

	for (Index = 0; Index < (uint8_t)(sizeof(ESP8266_BaudCandidates) / sizeof(ESP8266_BaudCandidates[0])); Index++)
	{
		if (ESP8266_ProbeBaudRate(ESP8266_BaudCandidates[Index], Response, Size) != 0U)
		{
			printf("ESP startup matched baud=%lu\r\n", ESP8266_BaudCandidates[Index]);
			return 1;
		}
	}

	printf("ESP startup no response on all baud rates\r\n");
	return 0;
}

uint8_t ESP8266_EnsureBaudRate(uint32_t TargetBaudRate, char *Response, uint16_t Size) /* 如果需要则自动切换 ESP 波特率。 */
{
	uint32_t DetectedBaudRate;

	if (ESP8266_RunStartupTest(Response, Size) == 0U)
	{
		return 0;
	}

	DetectedBaudRate = ESP8266_BaudRate;
	printf("ESP detected baud=%lu\r\n", DetectedBaudRate);

	if (DetectedBaudRate == TargetBaudRate)
	{
		printf("ESP baud already %lu\r\n", TargetBaudRate);
		return 1;
	}

	printf("ESP switching baud %lu -> %lu\r\n", DetectedBaudRate, TargetBaudRate);
	if (ESP8266_SetBaudRate(TargetBaudRate, Response, Size) == 0U)
	{
		printf("ESP baud switch command failed\r\n");
		if (ESP8266_LastResponseLength != 0U)
		{
			ESP8266_PrintResponseHex(Response, ESP8266_LastResponseLength);
		}
		return 0;
	}

	Delay_ms(300);
	ESP8266_Init(TargetBaudRate);
	Delay_ms(300);

	if (ESP8266_CheckAlive(Response, Size) != 0U)
	{
		printf("ESP baud switched to %lu\r\n", TargetBaudRate);
		return 1;
	}

	printf("ESP baud verify failed at %lu\r\n", TargetBaudRate);
	if (ESP8266_LastResponseLength != 0U)
	{
		ESP8266_PrintResponseHex(Response, ESP8266_LastResponseLength);
	}
	return 0;
}

uint8_t ESP8266_CheckAlive(char *Response, uint16_t Size) /* 用多种换行组合检查 ESP 是否响应 AT。 */
{
	static const char *Commands[] = {"AT\r\n", "AT\r", "AT\n", "AT"};
	uint8_t Index;

	for (Index = 0; Index < (uint8_t)(sizeof(Commands) / sizeof(Commands[0])); Index++)
	{
		printf("ESP alive cmd variant=%u\r\n", (unsigned int)(Index + 1U));
		if (ESP8266_SendCommandEx(Commands[Index], "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS) != 0U)
		{
			return 1;
		}

		Delay_ms(50);
	}

	return 0;
}

uint8_t ESP8266_DisableEcho(char *Response, uint16_t Size) /* 关闭 ESP 命令回显。 */
{
	return ESP8266_SendCommandEx("ATE0\r\n", "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS); /* 发送 ATE0 并等待 OK。 */
}

uint8_t ESP8266_SetWiFiModeStation(char *Response, uint16_t Size) /* 设置 ESP 为 STA 模式。 */
{
	return ESP8266_SendCommandEx("AT+CWMODE=1\r\n", "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS); /* 发送 CWMODE=1 并等待 OK。 */
}

uint8_t ESP8266_JoinAP(const char *Ssid, const char *Password, char *Response, uint16_t Size) /* 连接指定 WiFi 接入点。 */
{
	char Command[160];
	int Length;

	Length = sprintf(Command, "AT+CWJAP=\"%s\",\"%s\"\r\n", Ssid, Password);
	if ((Length <= 0) || (Length >= (int)sizeof(Command)))
	{
		if (Size != 0U)
		{
			Response[0] = '\0';
		}
		ESP8266_LastResponseLength = 0U;
		return 0U;
	}

	printf("ESP join AP ssid=%s\r\n", Ssid);
	printf("ESP8266 tx bytes hidden for WiFi credentials\r\n");
	return ESP8266_SendCommandInternal(Command, "OK", Response, Size, ESP8266_WIFI_JOIN_TIMEOUT_MS, 0U);
}

uint8_t ESP8266_QueryIP(char *Response, uint16_t Size) /* 查询当前 STA IP。 */
{
	return ESP8266_SendCommandEx("AT+CIFSR\r\n", "OK", Response, Size, ESP8266_WIFI_QUERY_TIMEOUT_MS);
}

uint8_t ESP8266_SetMultipleConnections(char *Response, uint16_t Size) /* 使能 TCP 多连接模式。 */
{
	return ESP8266_SendCommandEx("AT+CIPMUX=1\r\n", "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS);
}

uint8_t ESP8266_StartTCPServer(uint16_t Port, char *Response, uint16_t Size) /* 启动 TCP Server。 */
{
	char Command[32];

	sprintf(Command, "AT+CIPSERVER=1,%u\r\n", (unsigned int)Port);
	return ESP8266_SendCommandEx(Command, "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS);
}

uint8_t ESP8266_SendTCPData(uint8_t LinkId, const uint8_t *Data, uint16_t Length, char *Response, uint16_t Size) /* 使用 CIPSEND 向指定连接发送 TCP 数据。 */
{
	char Command[32];
	uint16_t ResponseLength = 0U;
	uint32_t ElapsedMs = 0U;
	uint8_t RxData;

	if ((Data == 0) || (Length == 0U) || (Size == 0U))
	{
		ESP8266_LastResponseLength = 0U;
		return 0U;
	}

	sprintf(Command, "AT+CIPSEND=%u,%u\r\n", (unsigned int)LinkId, (unsigned int)Length);
	Serial_ClearRxBuffer(USART2);
	Response[0] = '\0';
	ESP8266_PrintBytes("ESP8266 tx bytes", (const uint8_t *)Command, (uint16_t)strlen(Command));
	Serial_SendStringBlocking(USART2, Command);

	while (ElapsedMs < ESP8266_SHORT_TIMEOUT_MS)
	{
		while (Serial_ReadByteNonBlocking(USART2, &RxData) != 0U)
		{
			ESP8266_AppendByte(Response, Size, &ResponseLength, RxData);
			if (strchr(Response, '>') != NULL)
			{
				ESP8266_PrintBytes("ESP8266 tx payload", Data, Length);
				Serial_SendBufferBlocking(USART2, Data, Length);
				Response[0] = '\0';
				ResponseLength = 0U;
				ElapsedMs = 0U;

				while (ElapsedMs < ESP8266_WIFI_QUERY_TIMEOUT_MS)
				{
					while (Serial_ReadByteNonBlocking(USART2, &RxData) != 0U)
					{
						ESP8266_AppendByte(Response, Size, &ResponseLength, RxData);
						if (strstr(Response, "SEND OK") != NULL)
						{
							ESP8266_LastResponseLength = ResponseLength;
							return 1U;
						}

						if ((strstr(Response, "ERROR") != NULL) || (strstr(Response, "FAIL") != NULL))
						{
							ESP8266_LastResponseLength = ResponseLength;
							return 0U;
						}
					}

					Delay_ms(1U);
					ElapsedMs++;
				}

				ESP8266_LastResponseLength = ResponseLength;
				return 0U;
			}

			if ((strstr(Response, "ERROR") != NULL) || (strstr(Response, "FAIL") != NULL))
			{
				ESP8266_LastResponseLength = ResponseLength;
				return 0U;
			}
		}

		Delay_ms(1U);
		ElapsedMs++;
	}

	ESP8266_LastResponseLength = ResponseLength;
	return 0U;
}

uint8_t ESP8266_MQTTUserConfig(uint8_t LinkId, const char *ClientId, const char *Username, const char *Password, char *Response, uint16_t Size) /* 配置 MQTT 客户端身份信息。 */
{
	char EscapedClientId[80];
	char EscapedUsername[80];
	char EscapedPassword[80];
	char Command[320];

	if ((ESP8266_EscapeQuotedString(EscapedClientId, sizeof(EscapedClientId), ClientId) == 0U) ||
	    (ESP8266_EscapeQuotedString(EscapedUsername, sizeof(EscapedUsername), Username) == 0U) ||
	    (ESP8266_EscapeQuotedString(EscapedPassword, sizeof(EscapedPassword), Password) == 0U))
	{
		if (Size != 0U)
		{
			Response[0] = '\0';
		}
		ESP8266_LastResponseLength = 0U;
		return 0U;
	}

	sprintf(Command,
	        "AT+MQTTUSERCFG=%u,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
	        (unsigned int)LinkId,
	        EscapedClientId,
	        EscapedUsername,
	        EscapedPassword);
	return ESP8266_SendCommandEx(Command, "OK", Response, Size, ESP8266_MQTT_TIMEOUT_MS);
}

uint8_t ESP8266_MQTTConnect(uint8_t LinkId, const char *Host, uint16_t Port, char *Response, uint16_t Size) /* 建立到 MQTT Broker 的连接。 */
{
	char EscapedHost[96];
	char Command[160];

	if (ESP8266_EscapeQuotedString(EscapedHost, sizeof(EscapedHost), Host) == 0U)
	{
		if (Size != 0U)
		{
			Response[0] = '\0';
		}
		ESP8266_LastResponseLength = 0U;
		return 0U;
	}

	sprintf(Command,
	        "AT+MQTTCONN=%u,\"%s\",%u,0\r\n",
	        (unsigned int)LinkId,
	        EscapedHost,
	        (unsigned int)Port);
	return ESP8266_SendCommandEx(Command, "OK", Response, Size, ESP8266_MQTT_CONN_TIMEOUT_MS);
}

uint8_t ESP8266_MQTTSubscribe(uint8_t LinkId, const char *Topic, uint8_t QoS, char *Response, uint16_t Size) /* 订阅一个 MQTT 主题。 */
{
	char EscapedTopic[128];
	char Command[208];

	if (ESP8266_EscapeQuotedString(EscapedTopic, sizeof(EscapedTopic), Topic) == 0U)
	{
		if (Size != 0U)
		{
			Response[0] = '\0';
		}
		ESP8266_LastResponseLength = 0U;
		return 0U;
	}

	sprintf(Command,
	        "AT+MQTTSUB=%u,\"%s\",%u\r\n",
	        (unsigned int)LinkId,
	        EscapedTopic,
	        (unsigned int)QoS);
	return ESP8266_SendCommandEx(Command, "OK", Response, Size, ESP8266_MQTT_TIMEOUT_MS);
}

uint8_t ESP8266_MQTTClean(uint8_t LinkId, char *Response, uint16_t Size) /* 清理指定 LinkId 的 MQTT 会话。 */
{
	char Command[32];

	sprintf(Command, "AT+MQTTCLEAN=%u\r\n", (unsigned int)LinkId);
	return ESP8266_SendCommandEx(Command, "OK", Response, Size, ESP8266_MQTT_TIMEOUT_MS);
}

uint8_t ESP8266_MQTTPublish(uint8_t LinkId, const char *Topic, const char *Payload, uint8_t QoS, uint8_t Retain, char *Response, uint16_t Size) /* 发布一条 MQTT 消息。 */
{
	char EscapedTopic[128];
	char EscapedPayload[256];
	char Command[420];

	if ((ESP8266_EscapeQuotedString(EscapedTopic, sizeof(EscapedTopic), Topic) == 0U) ||
	    (ESP8266_EscapeQuotedString(EscapedPayload, sizeof(EscapedPayload), Payload) == 0U))
	{
		if (Size != 0U)
		{
			Response[0] = '\0';
		}
		ESP8266_LastResponseLength = 0U;
		return 0U;
	}

	sprintf(Command,
	        "AT+MQTTPUB=%u,\"%s\",\"%s\",%u,%u\r\n",
	        (unsigned int)LinkId,
	        EscapedTopic,
	        EscapedPayload,
	        (unsigned int)QoS,
	        (unsigned int)Retain);
	return ESP8266_SendCommandEx(Command, "OK", Response, Size, ESP8266_MQTT_TIMEOUT_MS);
}

uint32_t ESP8266_GetBaudRate(void) /* 返回当前记录的 ESP 波特率。 */
{
	return ESP8266_BaudRate;
}

uint16_t ESP8266_GetLastResponseLength(void) /* 返回最近一次响应的长度。 */
{
	return ESP8266_LastResponseLength;
}

void ESP8266_PrintResponseHex(const char *Buffer, uint16_t Length) /* 打印最近响应的原始十六进制字节。 */
{
	uint16_t Index;

	if ((Buffer == 0) || (Length == 0U))
	{
		return;
	}

	printf("ESP8266 raw bytes (%u):", Length);
	for (Index = 0; Index < Length; Index++)
	{
		printf(" %02X", (unsigned int)(uint8_t)Buffer[Index]);
	}
	printf("\r\n");
}
