#include "MQTTX.H" /* 包含 MQTT 业务模块头文件。 */
#include "esp8266.h" /* 包含 ESP8266 AT 命令封装接口。 */
#include "OLED.h" /* 包含 OLED 显示接口。 */
#include "LED.h" /* 包含 LED 控制接口。 */
#include "BH1750.h" /* 包含 BH1750 光照传感器接口。 */
#include "Sth30.h" /* 包含 STH30 温湿度传感器接口。 */
#include "app_config.h" /* 包含工程级串口功能开关。 */
#include <stdio.h> /* 包含 printf/sprintf 声明。 */
#include <string.h> /* 包含字符串处理接口。 */

static void MQTTX_ShowLine(uint8_t Line, const char *Text) /* 覆盖 OLED 指定行文本。 */
{
	OLED_ShowString(Line, 1, "                "); /* 先写空格清掉整行旧内容。 */
	OLED_ShowString(Line, 1, (char *)Text); /* 再把新文本写到首列。 */
}

static void MQTTX_PrintResponseOrEmpty(const char *Response) /* 打印 ESP 文本响应或空响应提示。 */
{
	if (Response[0] != '\0') /* 如果响应缓冲区里确实有内容。 */
	{
		printf("%s\r\n", Response); /* 先打印可读文本。 */
		ESP8266_PrintResponseHex(Response, ESP8266_GetLastResponseLength()); /* 再打印原始十六进制字节。 */
	}
	else
	{
		printf("(empty)\r\n"); /* 没收到任何文本时打印空响应提示。 */
	}
}

static void MQTTX_ShowTelemetry(const STH30_DataTypeDef *Climate, uint16_t Lux, uint8_t LedState) /* 在 OLED 上显示当前在线遥测信息。 */
{
	char Line[17]; /* 定义一行 OLED 文本缓存。 */
	int16_t Temperature = Climate->Temperature; /* 取出温度原始值，单位为 0.1 摄氏度。 */
	uint16_t TemperatureFraction = (uint16_t)((Temperature >= 0) ? (Temperature % 10) : (-Temperature % 10)); /* 计算温度小数部分。 */

	MQTTX_ShowLine(1, "MQTT ONLINE"); /* 第一行显示 MQTT 在线状态。 */
	sprintf(Line, "T:%d.%uC H:%u%%", Temperature / 10, TemperatureFraction, Climate->Humidity / 10); /* 生成温湿度摘要文本。 */
	MQTTX_ShowLine(2, Line); /* 在第二行显示温湿度摘要。 */
	sprintf(Line, "L:%ulx LED:%s", (unsigned int)Lux, (LedState != 0U) ? "ON" : "OFF"); /* 生成光照和 LED 状态文本。 */
	MQTTX_ShowLine(3, Line); /* 在第三行显示光照和 LED 状态。 */
	MQTTX_ShowLine(4, "CMD READY"); /* 第四行提示当前可以接收控制命令。 */
}

static uint8_t MQTTX_ParseLedCommand(const char *Payload, uint8_t *LedState) /* 从下行 payload 中提取 LED 控制值。 */
{
	const char *Cursor = Payload; /* 定义遍历 payload 的指针。 */

	while (*Cursor != '\0') /* 从头到尾扫描整条 payload。 */
	{
		if (((Cursor[0] == 'l') || (Cursor[0] == 'L')) && /* 忽略大小写寻找 led 关键字。 */
		    ((Cursor[1] == 'e') || (Cursor[1] == 'E')) &&
		    ((Cursor[2] == 'd') || (Cursor[2] == 'D')))
		{
			while ((*Cursor != '\0') && (*Cursor != '0') && (*Cursor != '1')) /* 找到关键字后继续向后定位 0 或 1。 */
			{
				Cursor++; /* 跳过冒号、空格、引号等无关字符。 */
			}

			if (*Cursor == '0') /* 如果 payload 中给出 LED=0。 */
			{
				*LedState = 0U; /* 更新 LED 目标状态为关。 */
				return 1U; /* 返回成功。 */
			}

			if (*Cursor == '1') /* 如果 payload 中给出 LED=1。 */
			{
				*LedState = 1U; /* 更新 LED 目标状态为开。 */
				return 1U; /* 返回成功。 */
			}
		}

		Cursor++; /* 当前字符不是有效命令则继续向后扫描。 */
	}

	return 0U; /* 没找到支持的 LED 命令则返回失败。 */
}

static uint8_t MQTTX_IsAsciiSpace(uint8_t Data) /* 判断字节是否为空格或制表符。 */
{
	return ((Data == ' ') || (Data == '\t')) ? 1U : 0U; /* 是空白字符则返回 1。 */
}

static uint8_t MQTTX_IsAsciiLetterEqualIgnoreCase(uint8_t Data, char Letter) /* 忽略大小写比较一个 ASCII 字母。 */
{
	if ((Data >= 'a') && (Data <= 'z')) /* 如果当前字节是小写字母。 */
	{
		Data = (uint8_t)(Data - ('a' - 'A')); /* 先转换成大写再参与比较。 */
	}

	return (Data == (uint8_t)Letter) ? 1U : 0U; /* 返回比较结果。 */
}

static uint8_t MQTTX_ConsumeWiFiSeparator(const uint8_t **Cursor, uint8_t AsciiSeparator) /* 消费 WIFI 命令中的英文或全角分隔符。 */
{
	if (**Cursor == AsciiSeparator) /* 如果当前位置就是英文分隔符。 */
	{
		(*Cursor)++; /* 消费一个英文分隔符。 */
		return 1U; /* 返回成功。 */
	}

	if (((*Cursor)[0] == 0xA3U) && /* 如果当前位置是全角分隔符的第二、三字节区域。 */
	    (((AsciiSeparator == ':') && ((*Cursor)[1] == 0xBAU)) ||
	     ((AsciiSeparator == ',') && ((*Cursor)[1] == 0xACU))))
	{
		*Cursor += 2; /* 消费 GBK/双字节里的全角冒号或逗号尾部两个字节。 */
		return 1U; /* 返回成功。 */
	}

	return 0U; /* 当前位置不是目标分隔符。 */
}

static uint8_t MQTTX_IsWiFiSeparatorAt(const uint8_t *Cursor, uint8_t AsciiSeparator) /* 判断当前位置是否为英文或全角分隔符。 */
{
	if (*Cursor == AsciiSeparator) /* 如果是英文分隔符。 */
	{
		return 1U; /* 返回成功。 */
	}

	if (((Cursor[0] == 0xA3U) && /* 如果是全角冒号或逗号的双字节尾部。 */
	     (((AsciiSeparator == ':') && (Cursor[1] == 0xBAU)) ||
	      ((AsciiSeparator == ',') && (Cursor[1] == 0xACU)))))
	{
		return 1U; /* 返回成功。 */
	}

	return 0U; /* 否则返回失败。 */
}

static uint8_t MQTTX_ParseWiFiCommand(const char *Command, char *Ssid, uint16_t SsidSize, char *Password, uint16_t PasswordSize) /* 解析 WIFI:ssid,password 命令。 */
{
	const uint8_t *Cursor = (const uint8_t *)Command; /* 定义按字节扫描命令的指针。 */
	const uint8_t *SsidStart; /* 保存 SSID 起始位置。 */
	const uint8_t *PasswordStart; /* 保存密码起始位置。 */
	uint16_t SsidLength; /* 保存 SSID 长度。 */
	uint16_t PasswordLength; /* 保存密码长度。 */

	while (MQTTX_IsAsciiSpace(*Cursor) != 0U) /* 跳过命令前导空白。 */
	{
		Cursor++; /* 移动到有效字符位置。 */
	}

	while (*Cursor != '\0') /* 在整条命令中寻找 WIFI 关键字。 */
	{
		if ((Cursor[1] != '\0') && /* 先确认后续字符足够构成 WIFI。 */
		    (Cursor[2] != '\0') &&
		    (Cursor[3] != '\0') &&
		    (MQTTX_IsAsciiLetterEqualIgnoreCase(Cursor[0], 'W') != 0U) &&
		    (MQTTX_IsAsciiLetterEqualIgnoreCase(Cursor[1], 'I') != 0U) &&
		    (MQTTX_IsAsciiLetterEqualIgnoreCase(Cursor[2], 'F') != 0U) &&
		    (MQTTX_IsAsciiLetterEqualIgnoreCase(Cursor[3], 'I') != 0U))
		{
			break; /* 找到 WIFI 关键字后退出搜索。 */
		}

		Cursor++; /* 当前字符不是目标关键字则继续向后搜索。 */
	}

	if ((*Cursor == '\0') || /* 如果没有找到完整 WIFI 关键字。 */
	    (Cursor[1] == '\0') ||
	    (Cursor[2] == '\0') ||
	    (Cursor[3] == '\0'))
	{
		return 0U; /* 返回失败表示命令格式不正确。 */
	}

	Cursor += 4; /* 跳过 WIFI 四个字符。 */
	while (MQTTX_IsAsciiSpace(*Cursor) != 0U) /* 跳过 WIFI 后面的空白。 */
	{
		Cursor++; /* 移动到分隔符位置。 */
	}

	if (MQTTX_ConsumeWiFiSeparator(&Cursor, ':') == 0U) /* 如果没有找到冒号分隔符。 */
	{
		return 0U; /* 返回失败。 */
	}

	while (MQTTX_IsAsciiSpace(*Cursor) != 0U) /* 跳过 SSID 前面的空白。 */
	{
		Cursor++; /* 移动到 SSID 起始位置。 */
	}

	SsidStart = Cursor; /* 记录 SSID 起始指针。 */
	while ((*Cursor != '\0') && (MQTTX_IsWiFiSeparatorAt(Cursor, ',') == 0U)) /* 一直扫到逗号分隔符。 */
	{
		Cursor++; /* 向后推进 SSID 结束位置。 */
	}

	if (*Cursor == '\0') /* 如果直到字符串结束都没找到逗号。 */
	{
		return 0U; /* 返回失败表示命令不完整。 */
	}

	SsidLength = (uint16_t)(Cursor - SsidStart); /* 计算 SSID 长度。 */
	while ((SsidLength > 0U) && (MQTTX_IsAsciiSpace(SsidStart[SsidLength - 1U]) != 0U)) /* 去掉 SSID 末尾空白。 */
	{
		SsidLength--; /* 缩短 SSID 逻辑长度。 */
	}

	if (MQTTX_ConsumeWiFiSeparator(&Cursor, ',') == 0U) /* 消费逗号分隔符。 */
	{
		return 0U; /* 失败则返回。 */
	}

	while (MQTTX_IsAsciiSpace(*Cursor) != 0U) /* 跳过密码前面的空白。 */
	{
		Cursor++; /* 移动到密码起始位置。 */
	}

	PasswordStart = Cursor; /* 记录密码起始指针。 */
	PasswordLength = (uint16_t)strlen((const char *)PasswordStart); /* 先按整段剩余字符串计算密码长度。 */
	while ((PasswordLength > 0U) && (MQTTX_IsAsciiSpace(PasswordStart[PasswordLength - 1U]) != 0U)) /* 去掉密码末尾空白。 */
	{
		PasswordLength--; /* 缩短密码逻辑长度。 */
	}

	if ((SsidLength == 0U) || (PasswordLength == 0U)) /* 如果 SSID 或密码为空。 */
	{
		return 0U; /* 返回失败。 */
	}

	if ((SsidLength >= SsidSize) || (PasswordLength >= PasswordSize)) /* 如果目标缓存装不下解析结果。 */
	{
		return 0U; /* 返回失败，防止缓冲区溢出。 */
	}

	memcpy(Ssid, SsidStart, SsidLength); /* 复制 SSID 到输出缓冲区。 */
	Ssid[SsidLength] = '\0'; /* 补上 SSID 结束符。 */
	memcpy(Password, PasswordStart, PasswordLength); /* 复制密码到输出缓冲区。 */
	Password[PasswordLength] = '\0'; /* 补上密码结束符。 */
	return 1U; /* 返回成功表示解析完成。 */
}

static uint8_t MQTTX_ExtractStaIp(const char *Response, char *IpBuffer, uint16_t Size) /* 从 AT+CIFSR 响应中提取 STA IP。 */
{
	const char *Start; /* 保存 IP 文本起始位置。 */
	const char *End; /* 保存 IP 文本结束位置。 */
	uint16_t Length; /* 保存 IP 字符串长度。 */

	Start = strstr(Response, "+CIFSR:STAIP,\""); /* 在响应中定位 STAIP 字段。 */
	if (Start == NULL) /* 如果没找到 IP 字段。 */
	{
		return 0U; /* 返回失败。 */
	}

	Start += strlen("+CIFSR:STAIP,\""); /* 跳过字段前缀，指向 IP 第一个字符。 */
	End = strchr(Start, '"'); /* 定位 IP 结束引号。 */
	if ((End == NULL) || (Size == 0U)) /* 如果结束引号缺失或输出缓存非法。 */
	{
		return 0U; /* 返回失败。 */
	}

	Length = (uint16_t)(End - Start); /* 计算 IP 字符串长度。 */
	if (Length >= Size) /* 如果输出缓存装不下整个 IP。 */
	{
		return 0U; /* 返回失败。 */
	}

	memcpy(IpBuffer, Start, Length); /* 复制 IP 文本到输出缓冲区。 */
	IpBuffer[Length] = '\0'; /* 补齐结束符。 */
	return 1U; /* 返回成功。 */
}

static uint8_t MQTTX_ParseSubHeader(const char *Header, uint8_t *LinkId, char *Topic, uint16_t TopicSize, uint16_t *PayloadLength) /* 解析 +MQTTSUBRECV 头部。 */
{
	const char *Cursor; /* 定义头部遍历指针。 */
	const char *TopicEnd; /* 定义 topic 结束引号位置。 */
	uint16_t Length; /* 保存 topic 长度。 */
	uint32_t Value; /* 保存解析数字时的中间值。 */

	if (strncmp(Header, "+MQTTSUBRECV:", 13U) != 0) /* 如果头部前缀不是目标格式。 */
	{
		return 0U; /* 返回失败。 */
	}

	Cursor = Header + 13; /* 跳到 link id 字段起始位置。 */
	if ((*Cursor < '0') || (*Cursor > '9')) /* 如果 link id 不是数字。 */
	{
		return 0U; /* 返回失败。 */
	}

	Value = 0U; /* 先把数字累加器清零。 */
	while ((*Cursor >= '0') && (*Cursor <= '9')) /* 解析 link id 数字。 */
	{
		Value = (Value * 10U) + (uint32_t)(*Cursor - '0'); /* 累加当前位数值。 */
		Cursor++; /* 指向下一个字符。 */
	}

	if ((*Cursor != ',') || (Value > 4U)) /* 如果 link id 后面不是逗号或值超出范围。 */
	{
		return 0U; /* 返回失败。 */
	}
	*LinkId = (uint8_t)Value; /* 保存解析出的 link id。 */
	Cursor++; /* 跳过逗号。 */

	if (*Cursor != '"') /* topic 必须从引号开始。 */
	{
		return 0U; /* 返回失败。 */
	}
	Cursor++; /* 跳过起始引号。 */

	TopicEnd = strchr(Cursor, '"'); /* 定位 topic 结束引号。 */
	if ((TopicEnd == NULL) || (TopicSize == 0U)) /* 如果 topic 结束引号缺失或缓存非法。 */
	{
		return 0U; /* 返回失败。 */
	}

	Length = (uint16_t)(TopicEnd - Cursor); /* 计算 topic 长度。 */
	if (Length >= TopicSize) /* 如果 topic 缓冲区不够大。 */
	{
		return 0U; /* 返回失败。 */
	}

	memcpy(Topic, Cursor, Length); /* 复制 topic 到输出缓冲区。 */
	Topic[Length] = '\0'; /* 补齐 topic 结束符。 */
	Cursor = TopicEnd + 1; /* 把指针移到 topic 结束引号之后。 */

	if (*Cursor != ',') /* topic 后面应当是逗号。 */
	{
		return 0U; /* 返回失败。 */
	}
	Cursor++; /* 跳过逗号，准备解析 payload 长度。 */

	if ((*Cursor < '0') || (*Cursor > '9')) /* 如果 payload 长度字段不是数字。 */
	{
		return 0U; /* 返回失败。 */
	}

	Value = 0U; /* 清零长度累加器。 */
	while ((*Cursor >= '0') && (*Cursor <= '9')) /* 解析 payload 长度。 */
	{
		Value = (Value * 10U) + (uint32_t)(*Cursor - '0'); /* 累加当前数字位。 */
		Cursor++; /* 指向下一个字符。 */
	}

	if ((*Cursor != '\0') || (Value == 0U)) /* 如果头部尾部还有多余字符或长度为 0。 */
	{
		return 0U; /* 返回失败。 */
	}

	*PayloadLength = (uint16_t)Value; /* 保存解析出的 payload 长度。 */
	return 1U; /* 返回成功。 */
}

static void MQTTX_ProcessCommand(const char *Topic, const char *Payload, uint8_t *LedState, char *Response, uint16_t ResponseSize) /* 处理订阅收到的控制命令。 */
{
	uint8_t NewLedState; /* 保存解析出的新 LED 状态。 */

	printf("\r\n[MQTT RX] topic=%s payload=%s\r\n", Topic, Payload); /* 打印接收到的主题和负载。 */
	if (strcmp(Topic, MQTTX_TOPIC_CMD) != 0) /* 如果收到的不是控制主题。 */
	{
		return; /* 直接忽略。 */
	}

	if (MQTTX_ParseLedCommand(Payload, &NewLedState) == 0U) /* 如果 payload 里没有支持的 LED 命令。 */
	{
		printf("[MQTT CMD] unsupported\r\n"); /* 打印不支持命令提示。 */
		MQTTX_ShowLine(4, "BAD CMD"); /* 在 OLED 上提示命令无效。 */
		return; /* 结束本次处理。 */
	}

	*LedState = NewLedState; /* 更新系统当前 LED 状态。 */
	if (*LedState != 0U) /* 如果目标状态为开。 */
	{
		LED1_ON(); /* 点亮 LED。 */
		MQTTX_ShowLine(4, "CMD LED ON"); /* OLED 提示 LED 已打开。 */
	}
	else
	{
		LED1_OFF(); /* 熄灭 LED。 */
		MQTTX_ShowLine(4, "CMD LED OFF"); /* OLED 提示 LED 已关闭。 */
	}

	printf("[MQTT CMD] led=%u\r\n", (unsigned int)*LedState); /* 打印当前 LED 状态。 */
	MQTTX_PublishStatus(*LedState, Response, ResponseSize); /* 控制执行后立即回发最新状态。 */
}

void MQTTX_InitContext(MQTTX_Context *Context) /* 初始化 MQTT 下行解析上下文。 */
{
	memset(Context, 0, sizeof(*Context)); /* 先把整个上下文清零。 */
	Context->State = MQTTX_SUB_IDLE; /* 把状态机置回空闲态。 */
}

static uint8_t MQTTX_WiFiConnectSequence(const char *Ssid, const char *Password, char *Response, uint16_t Size) /* 执行 WiFi 联网和 IP 获取流程。 */
{
	char StaIp[20]; /* 保存当前 STA IP 地址字符串。 */

	MQTTX_ShowLine(1, "ESP CHECK"); /* OLED 提示正在检查 ESP。 */
	MQTTX_ShowLine(2, "AT"); /* OLED 提示当前发送 AT。 */
	printf("[STEP] ESP alive check\r\n"); /* 优先按当前串口参数检查 ESP 是否已经能响应。 */
	if (ESP8266_CheckAlive(Response, Size) == 0U) /* 先确认 ESP AT 通道可用。 */
	{
		MQTTX_ShowLine(2, "BAUD SYNC"); /* OLED 提示进入波特率自恢复流程。 */
		printf("[STEP] ESP baud sync target=%lu\r\n", APP_ESP_BAUD_RATE); /* 串口打印即将执行的波特率同步动作。 */
		if (ESP8266_EnsureBaudRate(APP_ESP_BAUD_RATE, Response, Size) == 0U) /* 当前波特率失配时自动探测并切回工程目标波特率。 */
		{
			printf("[FAIL] ESP sync len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印自动同步失败。 */
			MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
			return 0U; /* 返回失败。 */
		}

		MQTTX_ShowLine(2, "AT"); /* 同步完成后把界面切回 AT 检查阶段。 */
		printf("[OK] ESP baud synced to %lu\r\n", APP_ESP_BAUD_RATE); /* 记录最终同步到的目标波特率。 */
	}

	MQTTX_ShowLine(2, "ATE0"); /* OLED 提示当前关闭回显。 */
	printf("[STEP] ATE0\r\n"); /* 串口打印当前步骤。 */
	if (ESP8266_DisableEcho(Response, Size) == 0U) /* 关闭回显，避免后续响应包含命令自身。 */
	{
		printf("[FAIL] ATE0 len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
		return 0U; /* 返回失败。 */
	}

	MQTTX_ShowLine(2, "CWMODE=1"); /* OLED 提示当前切到 STA 模式。 */
	printf("[STEP] AT+CWMODE=1\r\n"); /* 串口打印当前步骤。 */
	if (ESP8266_SetWiFiModeStation(Response, Size) == 0U) /* 设置 WiFi 工作模式为 Station。 */
	{
		printf("[FAIL] CWMODE len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
		return 0U; /* 返回失败。 */
	}

	MQTTX_ShowLine(1, "WIFI JOIN"); /* OLED 提示当前正在连接路由器。 */
	MQTTX_ShowLine(2, "CWJAP"); /* OLED 提示当前步骤是 CWJAP。 */
	printf("[STEP] AT+CWJAP ssid=%s\r\n", Ssid); /* 串口打印目标 SSID。 */
	if (ESP8266_JoinAP(Ssid, Password, Response, Size) == 0U) /* 连接指定 WiFi。 */
	{
		printf("[FAIL] CWJAP len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印联网失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印联网失败响应详情。 */
		return 0U; /* 返回失败。 */
	}
	printf("[OK] CWJAP len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印联网成功应答长度。 */
	MQTTX_PrintResponseOrEmpty(Response); /* 打印联网成功响应详情。 */

	MQTTX_ShowLine(2, "GET IP"); /* OLED 提示当前正在查询 IP。 */
	printf("[STEP] AT+CIFSR\r\n"); /* 串口打印查询 IP 步骤。 */
	if (ESP8266_QueryIP(Response, Size) == 0U) /* 查询当前 STA IP。 */
	{
		printf("[FAIL] CIFSR len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
		return 0U; /* 返回失败。 */
	}
	printf("[OK] CIFSR len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印获取 IP 成功信息。 */
	MQTTX_PrintResponseOrEmpty(Response); /* 打印完整 IP 响应。 */
	if (MQTTX_ExtractStaIp(Response, StaIp, sizeof(StaIp)) == 0U) /* 尝试从响应中提取 STA IP。 */
	{
		strcpy(StaIp, "UNKNOWN"); /* 如果提取失败则记录 UNKNOWN。 */
	}

	return MQTTX_Connect(Response, Size, StaIp); /* 联网成功后继续进入 MQTT 建链。 */
}

uint8_t MQTTX_Connect(char *Response, uint16_t Size, const char *StaIp) /* 执行 MQTT 清理、配置、连接和订阅。 */
{
	MQTTX_ShowLine(1, "MQTT CFG"); /* OLED 提示当前进入 MQTT 配置阶段。 */
	MQTTX_ShowLine(2, "USERCFG"); /* OLED 提示当前配置 MQTT 用户参数。 */
	printf("[STEP] AT+MQTTCLEAN=0 (best effort)\r\n"); /* 先尽力清理旧的 MQTT 会话。 */
	if (ESP8266_MQTTClean(MQTTX_LINK_ID, Response, Size) != 0U) /* 如果清理命令返回成功。 */
	{
		printf("[INFO] MQTTCLEAN len=%u\r\n", ESP8266_GetLastResponseLength()); /* 记录清理响应长度。 */
	}
	else
	{
		printf("[INFO] MQTTCLEAN skipped len=%u\r\n", ESP8266_GetLastResponseLength()); /* 清理失败也仅记录，不阻断流程。 */
	}

	printf("[STEP] AT+MQTTUSERCFG\r\n"); /* 打印 MQTT 用户配置步骤。 */
	if (ESP8266_MQTTUserConfig(MQTTX_LINK_ID, MQTTX_CLIENT_ID, "", "", Response, Size) == 0U) /* 配置 Client ID、用户名和密码。 */
	{
		printf("[FAIL] MQTTUSERCFG len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
		return 0U; /* 返回失败。 */
	}

	MQTTX_ShowLine(2, "MQTTCONN"); /* OLED 提示当前正在连接 Broker。 */
	printf("[STEP] AT+MQTTCONN %s:%u\r\n", MQTTX_BROKER_HOST, MQTTX_BROKER_PORT); /* 打印 Broker 地址。 */
	if (ESP8266_MQTTConnect(MQTTX_LINK_ID, MQTTX_BROKER_HOST, MQTTX_BROKER_PORT, Response, Size) == 0U) /* 发起 MQTT 连接。 */
	{
		printf("[FAIL] MQTTCONN len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印连接失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
		return 0U; /* 返回失败。 */
	}

	MQTTX_ShowLine(2, "MQTTSUB"); /* OLED 提示当前正在订阅控制主题。 */
	printf("[STEP] AT+MQTTSUB %s\r\n", MQTTX_TOPIC_CMD); /* 打印即将订阅的主题。 */
	if (ESP8266_MQTTSubscribe(MQTTX_LINK_ID, MQTTX_TOPIC_CMD, 0U, Response, Size) == 0U) /* 订阅控制主题。 */
	{
		printf("[FAIL] MQTTSUB len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印订阅失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
		return 0U; /* 返回失败。 */
	}

	printf("[MQTT READY] ip=%s broker=%s:%u\r\n", StaIp, MQTTX_BROKER_HOST, MQTTX_BROKER_PORT); /* 打印 MQTT 会话就绪信息。 */
	printf("[MQTT SUB] %s\r\n", MQTTX_TOPIC_CMD); /* 打印订阅主题。 */
	printf("[MQTT PUB] %s\r\n", MQTTX_TOPIC_TELEMETRY); /* 打印默认上报主题。 */
	return 1U; /* 返回成功表示 MQTT 已经准备就绪。 */
}

uint8_t MQTTX_StartSession(const char *Ssid, const char *Password, char *Response, uint16_t ResponseSize, uint8_t *LedState) /* 启动完整的 WiFi + MQTT 会话。 */
{
	printf("[CMD] connect ssid=%s\r\n", Ssid); /* 打印当前将要连接的 SSID。 */
	if (MQTTX_WiFiConnectSequence(Ssid, Password, Response, ResponseSize) != 0U) /* 如果 WiFi 与 MQTT 建链成功。 */
	{
		MQTTX_ShowLine(1, "MQTT ONLINE"); /* OLED 提示设备已在线。 */
		MQTTX_ShowLine(2, "PUB STATUS"); /* OLED 提示开始发布状态。 */
		if (MQTTX_PublishStatus(*LedState, Response, ResponseSize) == 0U) /* 先发布一次上线状态。 */
		{
			printf("[WARN] initial status publish failed\r\n"); /* 如果首次状态发布失败则仅记录警告。 */
		}

		MQTTX_ShowLine(2, "PUB TELEMETRY"); /* OLED 提示开始发布遥测。 */
		if (MQTTX_PublishTelemetry(*LedState, Response, ResponseSize) == 0U) /* 再立即发布一次遥测。 */
		{
			printf("[WARN] initial telemetry publish failed\r\n"); /* 如果首次遥测发布失败则仅记录警告。 */
		}
		return 1U; /* 返回成功表示会话已经建立。 */
	}

	LED1_OFF(); /* 会话建立失败时关闭 LED。 */
	MQTTX_ShowLine(1, "MQTT FAIL"); /* OLED 第一行提示失败。 */
#if (APP_USART1_ENABLE != 0U)
	MQTTX_ShowLine(2, "CHECK LOG"); /* OLED 第二行提示查看串口日志。 */
	MQTTX_ShowLine(3, "WIFI RETRY"); /* OLED 第三行提示可重新联网。 */
	MQTTX_ShowLine(4, "U1:WIFI:S,P"); /* OLED 第四行提示串口命令格式。 */
#else
	MQTTX_ShowLine(2, "CHECK WIFI"); /* USART1 关闭时提示检查自动联网配置。 */
	MQTTX_ShowLine(3, "SSID/PASS"); /* USART1 关闭时提示重点检查联网参数。 */
	MQTTX_ShowLine(4, "AUTO ONLY"); /* USART1 关闭时不再提示手动串口命令。 */
#endif
	return 0U; /* 返回失败。 */
}

uint8_t MQTTX_ExecuteWiFiCommand(const char *WiFiCommand,
                                 char *Ssid,
                                 uint16_t SsidSize,
                                 char *Password,
                                 uint16_t PasswordSize,
                                 char *Response,
                                 uint16_t ResponseSize,
                                 uint8_t *LedState) /* 执行串口输入的 WIFI 命令。 */
{
	uint16_t Index; /* 定义打印十六进制日志的循环索引。 */
	uint16_t Length; /* 保存原始命令长度。 */

	if (MQTTX_ParseWiFiCommand(WiFiCommand, Ssid, SsidSize, Password, PasswordSize) != 0U) /* 如果命令格式正确。 */
	{
		return MQTTX_StartSession(Ssid, Password, Response, ResponseSize, LedState); /* 直接启动 WiFi + MQTT 会话。 */
	}

	printf("[CMD] invalid format\r\n"); /* 打印命令格式错误提示。 */
	Length = (uint16_t)strlen(WiFiCommand); /* 计算用户输入的原始命令长度。 */
	printf("[CMD HEX]"); /* 打印原始命令字节，便于排查编码问题。 */
	for (Index = 0U; Index < Length; Index++) /* 逐字节输出命令十六进制。 */
	{
		printf(" %02X", (unsigned int)(uint8_t)WiFiCommand[Index]); /* 打印当前字节。 */
	}
	printf("\r\n"); /* 原始命令字节打印结束。 */
	printf("Use: WIFI:ssid,password\r\n"); /* 打印正确命令格式。 */
	MQTTX_ShowLine(1, "BAD CMD"); /* OLED 提示命令错误。 */
	MQTTX_ShowLine(2, "WIFI:S,P"); /* OLED 提示简写格式。 */
	MQTTX_ShowLine(4, "TRY AGAIN"); /* OLED 提示重新输入。 */
	return 0U; /* 返回失败。 */
}

uint8_t MQTTX_PublishStatus(uint8_t LedState, char *Response, uint16_t Size) /* 发布设备状态消息。 */
{
	char Payload[128]; /* 定义状态 JSON 负载缓冲区。 */

	sprintf(Payload, /* 生成纯数值状态 JSON，避免中文描述增加编码和长度负担。 */
	        "{\"online\":1,\"led\":%u}",
	        (unsigned int)LedState);
	printf("[STEP] MQTTPUB status\r\n"); /* 打印状态发布步骤。 */
	if (ESP8266_MQTTPublish(MQTTX_LINK_ID, MQTTX_TOPIC_STATUS, Payload, 0U, 0U, Response, Size) == 0U) /* 发布状态消息。 */
	{
		printf("[FAIL] status len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印状态发布失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
		return 0U; /* 返回失败。 */
	}

	printf("[OK] status len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印状态发布成功信息。 */
	return 1U; /* 返回成功。 */
}

uint8_t MQTTX_PublishTelemetry(uint8_t LedState, char *Response, uint16_t Size) /* 发布一次完整遥测消息。 */
{
	STH30_DataTypeDef Climate; /* 保存温湿度读取结果。 */
	uint16_t Lux; /* 保存光照读取结果。 */
	uint8_t SthResult; /* 保存 STH30 读取返回值。 */
	uint8_t LuxResult; /* 保存 BH1750 读取返回值。 */
	char Payload[128]; /* 纯数值遥测负载不再携带中文描述，缓冲区可相应缩小。 */
	int16_t Temperature; /* 保存温度原始值，单位为 0.1 摄氏度。 */
	uint16_t TemperatureFraction; /* 保存温度小数部分。 */

	SthResult = STH30_ReadData(&Climate); /* 读取温湿度传感器。 */
	LuxResult = BH1750_ReadLux(&Lux); /* 读取光照传感器。 */
	if ((SthResult != 0U) || (LuxResult != 0U)) /* 如果任意一个传感器读取失败。 */
	{
		printf("[WARN] sensor read fail sth30=%u bh1750=%u\r\n", /* 打印传感器读取失败信息。 */
		       (unsigned int)SthResult,
		       (unsigned int)LuxResult);
		MQTTX_ShowLine(1, "MQTT ONLINE"); /* OLED 第一行保留在线状态。 */
		MQTTX_ShowLine(2, "SENSOR FAIL"); /* OLED 第二行提示传感器失败。 */
		MQTTX_ShowLine(3, "CHECK I2C"); /* OLED 第三行提示检查 I2C。 */
		MQTTX_ShowLine(4, "PB6 PB7"); /* OLED 第四行提示检查引脚。 */
		return 0U; /* 返回失败。 */
	}

	Temperature = Climate.Temperature; /* 取出温度原始值。 */
	TemperatureFraction = (uint16_t)((Temperature >= 0) ? (Temperature % 10) : (-Temperature % 10)); /* 计算温度小数位。 */
	sprintf(Payload, /* 只上报数值字段，删除中文温度/湿度/光照描述。 */
	        "{\"temp\":%d.%u,\"humi\":%u.%u,\"lux\":%u,\"led\":%u}",
	        Temperature / 10,
	        TemperatureFraction,
	        Climate.Humidity / 10,
	        Climate.Humidity % 10,
	        (unsigned int)Lux,
	        (unsigned int)LedState);

	printf("[STEP] MQTTPUB telemetry\r\n"); /* 打印遥测发布步骤。 */
	if (ESP8266_MQTTPublish(MQTTX_LINK_ID, MQTTX_TOPIC_TELEMETRY, Payload, 0U, 0U, Response, Size) == 0U) /* 发布 telemetry 消息。 */
	{
		printf("[FAIL] telemetry len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印遥测发布失败信息。 */
		MQTTX_PrintResponseOrEmpty(Response); /* 打印失败响应详情。 */
		return 0U; /* 返回失败。 */
	}

	printf("[OK] telemetry len=%u\r\n", ESP8266_GetLastResponseLength()); /* 打印遥测发布成功信息。 */
	printf("[DATA] %s\r\n", Payload); /* 打印本次上报的 JSON 内容。 */
	MQTTX_ShowTelemetry(&Climate, Lux, LedState); /* 把本次遥测结果同步显示到 OLED。 */
	return 1U; /* 返回成功。 */
}

void MQTTX_ConsumeAsyncByte(MQTTX_Context *Context, uint8_t Data, uint8_t *LedState, char *Response, uint16_t ResponseSize) /* 消费 ESP 异步串口字节并解析下行命令。 */
{
	uint16_t PayloadLength; /* 保存解析出的 payload 长度。 */

	if (Context->State == MQTTX_SUB_PAYLOAD) /* 如果当前处于 payload 接收状态。 */
	{
		if (Context->PayloadStoredLength < (sizeof(Context->Payload) - 1U)) /* 只有缓冲区还有空间才保存新字节。 */
		{
			Context->Payload[Context->PayloadStoredLength++] = (char)Data; /* 追加当前 payload 字节。 */
			Context->Payload[Context->PayloadStoredLength] = '\0'; /* 维持字符串结束符。 */
		}

		Context->PayloadReceivedLength++; /* 记录 payload 总接收长度。 */
		if (Context->PayloadReceivedLength >= Context->PayloadExpectedLength) /* 如果已经收够整包 payload。 */
		{
			MQTTX_ProcessCommand(Context->Topic, Context->Payload, LedState, Response, ResponseSize); /* 处理完整下行命令。 */
			MQTTX_InitContext(Context); /* 命令处理完成后重置状态机。 */
		}
		return; /* 当前字节处理结束。 */
	}

	if ((Context->State == MQTTX_SUB_IDLE) && (Data == '+')) /* 空闲态遇到 '+' 说明可能进入订阅头部。 */
	{
		Context->State = MQTTX_SUB_HEADER; /* 切换到头部解析状态。 */
		Context->HeaderLength = 0U; /* 清空头部长度计数。 */
		Context->InQuotes = 0U; /* 清空引号状态。 */
		Context->CommaCount = 0U; /* 清空逗号计数。 */
	}

	if (Context->State != MQTTX_SUB_HEADER) /* 如果当前不在头部解析态。 */
	{
		return; /* 直接忽略当前字节。 */
	}

	if (Context->HeaderLength >= (sizeof(Context->Header) - 1U)) /* 如果头部缓存已经满。 */
	{
		MQTTX_InitContext(Context); /* 直接丢弃本次解析，防止缓冲区溢出。 */
		return; /* 结束当前字节处理。 */
	}

	Context->Header[Context->HeaderLength++] = (char)Data; /* 把当前字节追加到头部缓冲区。 */
	Context->Header[Context->HeaderLength] = '\0'; /* 始终维持头部字符串结束符。 */

	if (Data == '"') /* 如果遇到双引号。 */
	{
		Context->InQuotes = (uint8_t)(Context->InQuotes == 0U); /* 翻转当前是否在引号内部的状态。 */
	}
	else if ((Data == ',') && (Context->InQuotes == 0U)) /* 只统计引号外层的逗号。 */
	{
		Context->CommaCount++; /* 记录头部中的字段分隔逗号数量。 */
		if (Context->CommaCount >= 3U) /* 头部第三个逗号后即将进入 payload。 */
		{
			Context->Header[Context->HeaderLength - 1U] = '\0'; /* 把最后一个逗号改成结束符，便于解析头部。 */
			if (MQTTX_ParseSubHeader(Context->Header,
			                        &Context->LinkId,
			                        Context->Topic,
			                        sizeof(Context->Topic),
			                        &PayloadLength) != 0U) /* 如果头部解析成功。 */
			{
				Context->State = MQTTX_SUB_PAYLOAD; /* 切换到 payload 接收状态。 */
				Context->PayloadExpectedLength = PayloadLength; /* 记录期望 payload 长度。 */
				Context->PayloadReceivedLength = 0U; /* 清空已接收长度。 */
				Context->PayloadStoredLength = 0U; /* 清空已保存长度。 */
				Context->Payload[0] = '\0'; /* 清空 payload 文本。 */
				return; /* 头部解析成功，等待后续 payload 字节。 */
			}

			MQTTX_InitContext(Context); /* 头部格式不合法则重置状态机。 */
			return; /* 当前字节处理结束。 */
		}
	}

	if ((Data == '\r') || (Data == '\n')) /* 如果头部阶段提前遇到回车换行。 */
	{
		MQTTX_InitContext(Context); /* 说明当前不是有效的订阅头部，直接回到空闲态。 */
	}
}


