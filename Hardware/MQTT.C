#include "MQTTX.H"
#include "esp8266.h"
#include "OLED.h"
#include "LED.h"
#include "BH1750.h"
#include "Sth30.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

/* 将带符号的 0.1 摄氏度传感器值转换为适合显示的温度小数位。 */
static uint16_t MQTTX_TemperatureFraction(int16_t Temperature)
{
	/* C 语言中 % 会保留符号，因此这里将小数位统一规范化，便于显示和生成 JSON。 */
	return (uint16_t)((Temperature >= 0) ? (Temperature % 10) : (-Temperature % 10));
}

/* 以原子方式重写一整行 OLED，避免状态文本更新后残留旧字符。 */
static void MQTTX_ShowLine(uint8_t Line, const char *Text)
{
	OLED_ShowString(Line, 1, "                ");
	OLED_ShowString(Line, 1, (char *)Text);
}

/* 以可打印字符串和原始字节两种形式输出最近一次 ESP8266 的响应。 */
static void MQTTX_PrintResponseOrEmpty(const char *Response)
{
	if (Response[0] != '\0')
	{
		printf("%s\r\n", Response);
		ESP8266_PrintResponseHex(Response, ESP8266_GetLastResponseLength());
	}
	else
	{
		printf("(empty)\r\n");
	}
}

/* 遥测上报成功后，将当前在线状态快照显示到 OLED。 */
static void MQTTX_ShowTelemetry(const STH30_DataTypeDef *Climate, uint16_t Lux, uint8_t LedState)
{
	char Line[17];
	int16_t Temperature = Climate->Temperature;
	uint16_t TemperatureFraction = MQTTX_TemperatureFraction(Temperature);

	MQTTX_ShowLine(1, "MQTT ONLINE");
	sprintf(Line, "T:%d.%uC H:%u%%", Temperature / 10, TemperatureFraction, Climate->Humidity / 10);
	MQTTX_ShowLine(2, Line);
	sprintf(Line, "L:%ulx LED:%s", (unsigned int)Lux, (LedState != 0U) ? "ON" : "OFF");
	MQTTX_ShowLine(3, Line);
	MQTTX_ShowLine(4, "CMD READY");
}

/* 兼容多种宽松格式的载荷，例如 led=1、"LED":0 或 led : 1。 */
static uint8_t MQTTX_ParseLedCommand(const char *Payload, uint8_t *LedState)
{
	const char *Cursor = Payload;

	while (*Cursor != '\0')
	{
		if (((Cursor[0] == 'l') || (Cursor[0] == 'L')) &&
		    ((Cursor[1] == 'e') || (Cursor[1] == 'E')) &&
		    ((Cursor[2] == 'd') || (Cursor[2] == 'D')))
		{
			while ((*Cursor != '\0') && (*Cursor != '0') && (*Cursor != '1'))
			{
				Cursor++;
			}

			if (*Cursor == '0')
			{
				*LedState = 0U;
				return 1U;
			}

			if (*Cursor == '1')
			{
				*LedState = 1U;
				return 1U;
			}
		}

		Cursor++;
	}

	return 0U;
}

/* 仅用于解析 ASCII 命令的本地辅助函数。 */
static uint8_t MQTTX_IsAsciiSpace(uint8_t Data)
{
	return ((Data == ' ') || (Data == '\t')) ? 1U : 0U;
}

/* 不依赖 libc 区域设置规则的 ASCII 忽略大小写字母匹配辅助函数。 */
static uint8_t MQTTX_IsAsciiLetterEqualIgnoreCase(uint8_t Data, char Letter)
{
	if ((Data >= 'a') && (Data <= 'z'))
	{
		Data = (uint8_t)(Data - ('a' - 'A'));
	}

	return (Data == (uint8_t)Letter) ? 1U : 0U;
}

/*
 * 某些 Windows 串口工具在用户输入中文时会发送全角 GBK 标点，
 * 因此这里同时兼容 ASCII 和全角的 ':' 与 ','。
 */
static uint8_t MQTTX_ConsumeWiFiSeparator(const uint8_t **Cursor, uint8_t AsciiSeparator)
{
	if (**Cursor == AsciiSeparator)
	{
		(*Cursor)++;
		return 1U;
	}

	if (((*Cursor)[0] == 0xA3U) &&
	    (((AsciiSeparator == ':') && ((*Cursor)[1] == 0xBAU)) ||
	     ((AsciiSeparator == ',') && ((*Cursor)[1] == 0xACU))))
	{
		*Cursor += 2;
		return 1U;
	}

	return 0U;
}

/* 判断当前位置是否为 ASCII 或全角分隔符，但不消耗输入流。 */
static uint8_t MQTTX_IsWiFiSeparatorAt(const uint8_t *Cursor, uint8_t AsciiSeparator)
{
	if (*Cursor == AsciiSeparator)
	{
		return 1U;
	}

	if (((Cursor[0] == 0xA3U) &&
	     (((AsciiSeparator == ':') && (Cursor[1] == 0xBAU)) ||
	      ((AsciiSeparator == ',') && (Cursor[1] == 0xACU)))))
	{
		return 1U;
	}

	return 0U;
}

/* 去掉两端空白，并在复制到调用者缓冲区前拒绝超长字段。 */
static uint8_t MQTTX_ParseWiFiCommand(const char *Command, char *Ssid, uint16_t SsidSize, char *Password, uint16_t PasswordSize)
{
	const uint8_t *Cursor = (const uint8_t *)Command;
	const uint8_t *SsidStart;
	const uint8_t *PasswordStart;
	uint16_t SsidLength;
	uint16_t PasswordLength;

	while (MQTTX_IsAsciiSpace(*Cursor) != 0U)
	{
		Cursor++;
	}

	while (*Cursor != '\0')
	{
		if ((Cursor[1] != '\0') &&
		    (Cursor[2] != '\0') &&
		    (Cursor[3] != '\0') &&
		    (MQTTX_IsAsciiLetterEqualIgnoreCase(Cursor[0], 'W') != 0U) &&
		    (MQTTX_IsAsciiLetterEqualIgnoreCase(Cursor[1], 'I') != 0U) &&
		    (MQTTX_IsAsciiLetterEqualIgnoreCase(Cursor[2], 'F') != 0U) &&
		    (MQTTX_IsAsciiLetterEqualIgnoreCase(Cursor[3], 'I') != 0U))
		{
			break;
		}

		Cursor++;
	}

	if ((*Cursor == '\0') ||
	    (Cursor[1] == '\0') ||
	    (Cursor[2] == '\0') ||
	    (Cursor[3] == '\0'))
	{
		return 0U;
	}

	Cursor += 4;
	while (MQTTX_IsAsciiSpace(*Cursor) != 0U)
	{
		Cursor++;
	}

	if (MQTTX_ConsumeWiFiSeparator(&Cursor, ':') == 0U)
	{
		return 0U;
	}

	while (MQTTX_IsAsciiSpace(*Cursor) != 0U)
	{
		Cursor++;
	}

	SsidStart = Cursor;
	while ((*Cursor != '\0') && (MQTTX_IsWiFiSeparatorAt(Cursor, ',') == 0U))
	{
		Cursor++;
	}

	if (*Cursor == '\0')
	{
		return 0U;
	}

	SsidLength = (uint16_t)(Cursor - SsidStart);
	while ((SsidLength > 0U) && (MQTTX_IsAsciiSpace(SsidStart[SsidLength - 1U]) != 0U))
	{
		SsidLength--;
	}

	if (MQTTX_ConsumeWiFiSeparator(&Cursor, ',') == 0U)
	{
		return 0U;
	}

	while (MQTTX_IsAsciiSpace(*Cursor) != 0U)
	{
		Cursor++;
	}

	PasswordStart = Cursor;
	PasswordLength = (uint16_t)strlen((const char *)PasswordStart);
	while ((PasswordLength > 0U) && (MQTTX_IsAsciiSpace(PasswordStart[PasswordLength - 1U]) != 0U))
	{
		PasswordLength--;
	}

	if ((SsidLength == 0U) || (PasswordLength == 0U))
	{
		return 0U;
	}

	if ((SsidLength >= SsidSize) || (PasswordLength >= PasswordSize))
	{
		return 0U;
	}

	memcpy(Ssid, SsidStart, SsidLength);
	Ssid[SsidLength] = '\0';
	memcpy(Password, PasswordStart, PasswordLength);
	Password[PasswordLength] = '\0';
	return 1U;
}

/* 从 AT+CIFSR 响应中提取 STA IP，便于后续日志标识当前网络状态。 */
static uint8_t MQTTX_ExtractStaIp(const char *Response, char *IpBuffer, uint16_t Size)
{
	const char *Start;
	const char *End;
	uint16_t Length;

	Start = strstr(Response, "+CIFSR:STAIP,\"");
	if (Start == NULL)
	{
		return 0U;
	}

	Start += strlen("+CIFSR:STAIP,\"");
	End = strchr(Start, '"');
	if ((End == NULL) || (Size == 0U))
	{
		return 0U;
	}

	Length = (uint16_t)(End - Start);
	if (Length >= Size)
	{
		return 0U;
	}

	memcpy(IpBuffer, Start, Length);
	IpBuffer[Length] = '\0';
	return 1U;
}

/* 在按字节状态机开始读取载荷之前，先解析 +MQTTSUBRECV 的头部。 */
static uint8_t MQTTX_ParseSubHeader(const char *Header, uint8_t *LinkId, char *Topic, uint16_t TopicSize, uint16_t *PayloadLength)
{
	const char *Cursor;
	const char *TopicEnd;
	uint16_t Length;
	uint32_t Value;

	if (strncmp(Header, "+MQTTSUBRECV:", 13U) != 0)
	{
		return 0U;
	}

	Cursor = Header + 13;
	if ((*Cursor < '0') || (*Cursor > '9'))
	{
		return 0U;
	}

	Value = 0U;
	while ((*Cursor >= '0') && (*Cursor <= '9'))
	{
		Value = (Value * 10U) + (uint32_t)(*Cursor - '0');
		Cursor++;
	}

	/* ESP-AT 上报的 MQTT 链路 ID 范围是 0..4。 */
	if ((*Cursor != ',') || (Value > 4U))
	{
		return 0U;
	}
	*LinkId = (uint8_t)Value;
	Cursor++;

	if (*Cursor != '"')
	{
		return 0U;
	}
	Cursor++;

	TopicEnd = strchr(Cursor, '"');
	if ((TopicEnd == NULL) || (TopicSize == 0U))
	{
		return 0U;
	}

	Length = (uint16_t)(TopicEnd - Cursor);
	if (Length >= TopicSize)
	{
		return 0U;
	}

	memcpy(Topic, Cursor, Length);
	Topic[Length] = '\0';
	Cursor = TopicEnd + 1;

	if (*Cursor != ',')
	{
		return 0U;
	}
	Cursor++;

	if ((*Cursor < '0') || (*Cursor > '9'))
	{
		return 0U;
	}

	Value = 0U;
	while ((*Cursor >= '0') && (*Cursor <= '9'))
	{
		Value = (Value * 10U) + (uint32_t)(*Cursor - '0');
		Cursor++;
	}

	if ((*Cursor != '\0') || (Value == 0U))
	{
		return 0U;
	}

	*PayloadLength = (uint16_t)Value;
	return 1U;
}

/* 处理订阅到的控制消息，并立即回显设备当前状态。 */
static void MQTTX_ProcessCommand(const char *Topic, const char *Payload, uint8_t *LedState, char *Response, uint16_t ResponseSize)
{
	uint8_t NewLedState;

	printf("\r\n[MQTT RX] topic=%s payload=%s\r\n", Topic, Payload);
	if (strcmp(Topic, MQTTX_TOPIC_CMD) != 0)
	{
		return;
	}

	if (MQTTX_ParseLedCommand(Payload, &NewLedState) == 0U)
	{
		printf("[MQTT CMD] unsupported\r\n");
		MQTTX_ShowLine(4, "BAD CMD");
		return;
	}

	*LedState = NewLedState;
	if (*LedState != 0U)
	{
		LED1_ON();
		MQTTX_ShowLine(4, "CMD LED ON");
	}
	else
	{
		LED1_OFF();
		MQTTX_ShowLine(4, "CMD LED OFF");
	}

	printf("[MQTT CMD] led=%u\r\n", (unsigned int)*LedState);
	MQTTX_PublishStatus(*LedState, Response, ResponseSize);
}

/* 将异步订阅解析器上下文重置为空闲状态。 */
void MQTTX_InitContext(MQTTX_Context *Context)
{
	memset(Context, 0, sizeof(*Context));
	Context->State = MQTTX_SUB_IDLE;
}

/* 让 ESP8266 从 AT 可用状态进入已连接 Wi-Fi 且已获取 STA IP 的状态。 */
static uint8_t MQTTX_WiFiConnectSequence(const char *Ssid, const char *Password, char *Response, uint16_t Size)
{
	char StaIp[20];

	MQTTX_ShowLine(1, "ESP CHECK");
	MQTTX_ShowLine(2, "AT");
	printf("[STEP] ESP alive check\r\n");
	if (ESP8266_CheckAlive(Response, Size) == 0U)
	{
		MQTTX_ShowLine(2, "BAUD SYNC");
		printf("[STEP] ESP baud sync target=%lu\r\n", APP_ESP_BAUD_RATE);
		if (ESP8266_EnsureBaudRate(APP_ESP_BAUD_RATE, Response, Size) == 0U)
		{
			printf("[FAIL] ESP sync len=%u\r\n", ESP8266_GetLastResponseLength());
			MQTTX_PrintResponseOrEmpty(Response);
			return 0U;
		}

		MQTTX_ShowLine(2, "AT");
		printf("[OK] ESP baud synced to %lu\r\n", APP_ESP_BAUD_RATE);
	}

	MQTTX_ShowLine(2, "ATE0");
	printf("[STEP] ATE0\r\n");
	if (ESP8266_DisableEcho(Response, Size) == 0U)
	{
		printf("[FAIL] ATE0 len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}

	MQTTX_ShowLine(2, "CWMODE=1");
	printf("[STEP] AT+CWMODE=1\r\n");
	if (ESP8266_SetWiFiModeStation(Response, Size) == 0U)
	{
		printf("[FAIL] CWMODE len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}

	MQTTX_ShowLine(1, "WIFI JOIN");
	MQTTX_ShowLine(2, "CWJAP");
	printf("[STEP] AT+CWJAP ssid=%s\r\n", Ssid);
	if (ESP8266_JoinAP(Ssid, Password, Response, Size) == 0U)
	{
		printf("[FAIL] CWJAP len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}
	printf("[OK] CWJAP len=%u\r\n", ESP8266_GetLastResponseLength());
	MQTTX_PrintResponseOrEmpty(Response);

	MQTTX_ShowLine(2, "GET IP");
	printf("[STEP] AT+CIFSR\r\n");
	if (ESP8266_QueryIP(Response, Size) == 0U)
	{
		printf("[FAIL] CIFSR len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}
	printf("[OK] CIFSR len=%u\r\n", ESP8266_GetLastResponseLength());
	MQTTX_PrintResponseOrEmpty(Response);
	if (MQTTX_ExtractStaIp(Response, StaIp, sizeof(StaIp)) == 0U)
	{
		strcpy(StaIp, "UNKNOWN");
	}

	return MQTTX_Connect(Response, Size, StaIp);
}

/* 建立全新的 MQTT 会话：清理旧状态、配置客户端、连接 Broker，并完成订阅。 */
uint8_t MQTTX_Connect(char *Response, uint16_t Size, const char *StaIp)
{
	MQTTX_ShowLine(1, "MQTT CFG");
	MQTTX_ShowLine(2, "USERCFG");
	printf("[STEP] AT+MQTTCLEAN=0 (best effort)\r\n");
	/*
	 * 模块复位后，Broker 端可能残留旧会话。
	 * 清理命令成功时会有帮助，但即使模块返回错误，也应继续尝试重连。
	 */
	if (ESP8266_MQTTClean(MQTTX_LINK_ID, Response, Size) != 0U)
	{
		printf("[INFO] MQTTCLEAN len=%u\r\n", ESP8266_GetLastResponseLength());
	}
	else
	{
		printf("[INFO] MQTTCLEAN skipped len=%u\r\n", ESP8266_GetLastResponseLength());
	}

	printf("[STEP] AT+MQTTUSERCFG\r\n");
	if (ESP8266_MQTTUserConfig(MQTTX_LINK_ID, MQTTX_CLIENT_ID, "", "", Response, Size) == 0U)
	{
		printf("[FAIL] MQTTUSERCFG len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}

	MQTTX_ShowLine(2, "MQTTCONN");
	printf("[STEP] AT+MQTTCONN %s:%u\r\n", MQTTX_BROKER_HOST, MQTTX_BROKER_PORT);
	if (ESP8266_MQTTConnect(MQTTX_LINK_ID, MQTTX_BROKER_HOST, MQTTX_BROKER_PORT, Response, Size) == 0U)
	{
		printf("[FAIL] MQTTCONN len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}

	MQTTX_ShowLine(2, "MQTTSUB");
	printf("[STEP] AT+MQTTSUB %s\r\n", MQTTX_TOPIC_CMD);
	if (ESP8266_MQTTSubscribe(MQTTX_LINK_ID, MQTTX_TOPIC_CMD, 0U, Response, Size) == 0U)
	{
		printf("[FAIL] MQTTSUB len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}

	printf("[MQTT READY] ip=%s broker=%s:%u\r\n", StaIp, MQTTX_BROKER_HOST, MQTTX_BROKER_PORT);
	printf("[MQTT SUB] %s\r\n", MQTTX_TOPIC_CMD);
	printf("[MQTT PUB] %s\r\n", MQTTX_TOPIC_TELEMETRY);
	return 1U;
}

/* 执行完整的 Wi-Fi + MQTT 启动流程，并发送首次在线状态与状态上报。 */
uint8_t MQTTX_StartSession(const char *Ssid, const char *Password, char *Response, uint16_t ResponseSize, uint8_t *LedState)
{
	printf("[CMD] connect ssid=%s\r\n", Ssid);
	if (MQTTX_WiFiConnectSequence(Ssid, Password, Response, ResponseSize) != 0U)
	{
		MQTTX_ShowLine(1, "MQTT ONLINE");
		MQTTX_ShowLine(2, "PUB STATUS");
		if (MQTTX_PublishStatus(*LedState, Response, ResponseSize) == 0U)
		{
			printf("[WARN] initial status publish failed\r\n");
		}

		MQTTX_ShowLine(2, "PUB TELEMETRY");
		if (MQTTX_PublishTelemetry(*LedState, Response, ResponseSize) == 0U)
		{
			printf("[WARN] initial telemetry publish failed\r\n");
		}
		return 1U;
	}

	LED1_OFF();
	MQTTX_ShowLine(1, "MQTT FAIL");
#if (APP_USART1_ENABLE != 0U)
	MQTTX_ShowLine(2, "CHECK LOG");
	MQTTX_ShowLine(3, "WIFI RETRY");
	MQTTX_ShowLine(4, "U1:WIFI:S,P");
#else
	MQTTX_ShowLine(2, "CHECK WIFI");
	MQTTX_ShowLine(3, "SSID/PASS");
	MQTTX_ShowLine(4, "AUTO ONLY");
#endif
	return 0U;
}

/* 校验手动输入的 WIFI:ssid,password 命令格式，并在合法时启动新会话。 */
uint8_t MQTTX_ExecuteWiFiCommand(const char *WiFiCommand,
                                 char *Ssid,
                                 uint16_t SsidSize,
                                 char *Password,
                                 uint16_t PasswordSize,
                                 char *Response,
                                 uint16_t ResponseSize,
                                 uint8_t *LedState)
{
	uint16_t Index;
	uint16_t Length;

	if (MQTTX_ParseWiFiCommand(WiFiCommand, Ssid, SsidSize, Password, PasswordSize) != 0U)
	{
		return MQTTX_StartSession(Ssid, Password, Response, ResponseSize, LedState);
	}

	printf("[CMD] invalid format\r\n");
	Length = (uint16_t)strlen(WiFiCommand);
	printf("[CMD HEX]");
	for (Index = 0U; Index < Length; Index++)
	{
		printf(" %02X", (unsigned int)(uint8_t)WiFiCommand[Index]);
	}
	printf("\r\n");
	printf("Use: WIFI:ssid,password\r\n");
	MQTTX_ShowLine(1, "BAD CMD");
	MQTTX_ShowLine(2, "WIFI:S,P");
	MQTTX_ShowLine(4, "TRY AGAIN");
	return 0U;
}

/* 上报供仪表盘与命令反馈使用的精简在线/LED 状态 JSON。 */
/* MCU 侧生成的载荷保持纯数字格式，避免区域设置和编码差异带来的问题。 */
uint8_t MQTTX_PublishStatus(uint8_t LedState, char *Response, uint16_t Size)
{
	char Payload[128];

	sprintf(Payload,
	        "{\"online\":1,\"led\":%u}",
	        (unsigned int)LedState);
	printf("[STEP] MQTTPUB status\r\n");
	if (ESP8266_MQTTPublish(MQTTX_LINK_ID, MQTTX_TOPIC_STATUS, Payload, 0U, 0U, Response, Size) == 0U)
	{
		printf("[FAIL] status len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}

	printf("[OK] status len=%u\r\n", ESP8266_GetLastResponseLength());
	return 1U;
}

/* 读取传感器，发布一帧遥测数据，并刷新本地 OLED 摘要显示。 */
uint8_t MQTTX_PublishTelemetry(uint8_t LedState, char *Response, uint16_t Size)
{
	STH30_DataTypeDef Climate;
	uint16_t Lux;
	uint8_t SthResult;
	uint8_t LuxResult;
	char Payload[128];
	int16_t Temperature;
	uint16_t TemperatureFraction;

	SthResult = STH30_ReadData(&Climate);
	LuxResult = BH1750_ReadLux(&Lux);
	if ((SthResult != 0U) || (LuxResult != 0U))
	{
		printf("[WARN] sensor read fail sth30=%u bh1750=%u\r\n",
		       (unsigned int)SthResult,
		       (unsigned int)LuxResult);
		MQTTX_ShowLine(1, "MQTT ONLINE");
		MQTTX_ShowLine(2, "SENSOR FAIL");
		MQTTX_ShowLine(3, "CHECK I2C");
		MQTTX_ShowLine(4, "PB6 PB7");
		return 0U;
	}

	Temperature = Climate.Temperature;
	TemperatureFraction = MQTTX_TemperatureFraction(Temperature);
	sprintf(Payload,
	        "{\"temp\":%d.%u,\"humi\":%u.%u,\"lux\":%u,\"led\":%u}",
	        Temperature / 10,
	        TemperatureFraction,
	        Climate.Humidity / 10,
	        Climate.Humidity % 10,
	        (unsigned int)Lux,
	        (unsigned int)LedState);

	printf("[STEP] MQTTPUB telemetry\r\n");
	if (ESP8266_MQTTPublish(MQTTX_LINK_ID, MQTTX_TOPIC_TELEMETRY, Payload, 0U, 0U, Response, Size) == 0U)
	{
		printf("[FAIL] telemetry len=%u\r\n", ESP8266_GetLastResponseLength());
		MQTTX_PrintResponseOrEmpty(Response);
		return 0U;
	}

	printf("[OK] telemetry len=%u\r\n", ESP8266_GetLastResponseLength());
	printf("[DATA] %s\r\n", Payload);
	MQTTX_ShowTelemetry(&Climate, Lux, LedState);
	return 1U;
}

/* 持续读取 ESP-AT 的订阅数据字节流，并重组出完整的 MQTT 下行报文。 */
/*
 * 即使 Payload[] 已满导致 PayloadStoredLength 不再增长，
 * 解析器仍会继续把 Broker 声明长度的全部载荷读完，以保持 AT 数据流对齐。
 */
void MQTTX_ConsumeAsyncByte(MQTTX_Context *Context, uint8_t Data, uint8_t *LedState, char *Response, uint16_t ResponseSize)
{
	uint16_t PayloadLength;

	if (Context->State == MQTTX_SUB_PAYLOAD)
	{
		if (Context->PayloadStoredLength < (sizeof(Context->Payload) - 1U))
		{
			Context->Payload[Context->PayloadStoredLength++] = (char)Data;
			Context->Payload[Context->PayloadStoredLength] = '\0';
		}

		Context->PayloadReceivedLength++;
		if (Context->PayloadReceivedLength >= Context->PayloadExpectedLength)
		{
			MQTTX_ProcessCommand(Context->Topic, Context->Payload, LedState, Response, ResponseSize);
			MQTTX_InitContext(Context);
		}
		return;
	}

	if ((Context->State == MQTTX_SUB_IDLE) && (Data == '+'))
	{
		Context->State = MQTTX_SUB_HEADER;
		Context->HeaderLength = 0U;
		Context->InQuotes = 0U;
		Context->CommaCount = 0U;
	}

	if (Context->State != MQTTX_SUB_HEADER)
	{
		return;
	}

	if (Context->HeaderLength >= (sizeof(Context->Header) - 1U))
	{
		MQTTX_InitContext(Context);
		return;
	}

	Context->Header[Context->HeaderLength++] = (char)Data;
	Context->Header[Context->HeaderLength] = '\0';

	if (Data == '"')
	{
		Context->InQuotes = (uint8_t)(Context->InQuotes == 0U);
	}
	else if ((Data == ',') && (Context->InQuotes == 0U))
	{
		Context->CommaCount++;
		if (Context->CommaCount >= 3U)
		{
			Context->Header[Context->HeaderLength - 1U] = '\0';
			if (MQTTX_ParseSubHeader(Context->Header,
			                        &Context->LinkId,
			                        Context->Topic,
			                        sizeof(Context->Topic),
			                        &PayloadLength) != 0U)
			{
				Context->State = MQTTX_SUB_PAYLOAD;
				Context->PayloadExpectedLength = PayloadLength;
				Context->PayloadReceivedLength = 0U;
				Context->PayloadStoredLength = 0U;
				Context->Payload[0] = '\0';
				return;
			}

			MQTTX_InitContext(Context);
			return;
		}
	}

	if ((Data == '\r') || (Data == '\n'))
	{
		MQTTX_InitContext(Context);
	}
}