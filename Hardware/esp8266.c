#include "esp8266.h"
#include <stdio.h>
#include <string.h>
#include "stm32f10x.h"
#include "Delay.h"
#include "serial.h"

static uint32_t ESP8266_BaudRate = 115200U;
static uint16_t ESP8266_LastResponseLength = 0U;

#define ESP8266_POWER_ON_DELAY_MS        2000U
#define ESP8266_PROBE_ATTEMPTS           3U
#define ESP8266_DEFAULT_TIMEOUT_MS       1000U
#define ESP8266_SHORT_TIMEOUT_MS         2000U
#define ESP8266_WIFI_JOIN_TIMEOUT_MS     20000U
#define ESP8266_WIFI_QUERY_TIMEOUT_MS    3000U
#define ESP8266_MQTT_TIMEOUT_MS          5000U
#define ESP8266_MQTT_CONN_TIMEOUT_MS     20000U

static const uint32_t ESP8266_BaudCandidates[] = {115200U, 9600U, 57600U, 38400U, 74880U, 19200U};

static void ESP8266_PrintBytes(const char *Prefix, const uint8_t *Buffer, uint16_t Length)
{
	uint16_t Index;

	printf("%s (%u):", Prefix, Length);
	for (Index = 0; Index < Length; Index++)
	{
		printf(" %02X", (unsigned int)Buffer[Index]);
	}
	printf("\r\n");
}

static void ESP8266_AppendByte(char *Buffer, uint16_t Size, uint16_t *Length, uint8_t Data)
{
	if (*Length < (uint16_t)(Size - 1U))
	{
		Buffer[*Length] = (char)Data;
		(*Length)++;
		Buffer[*Length] = '\0';
	}
}

static uint8_t ESP8266_EscapeQuotedString(char *Destination, uint16_t DestinationSize, const char *Source)
{
	uint16_t Index = 0U;

	if ((Destination == 0) || (Source == 0) || (DestinationSize == 0U))
	{
		return 0U;
	}

	while (*Source != '\0')
	{
		if ((*Source == '"') || (*Source == '\\') || (*Source == ','))
		{
			if ((Index + 2U) >= DestinationSize)
			{
				return 0U;
			}

			Destination[Index++] = '\\';
		}
		else if ((Index + 1U) >= DestinationSize)
		{
			return 0U;
		}

		Destination[Index++] = *Source;
		Source++;
	}

	Destination[Index] = '\0';
	return 1U;
}

static uint8_t ESP8266_SendCommandInternal(const char *Command,
                                          const char *Expected,
                                          char *Response,
                                          uint16_t Size,
                                          uint32_t TimeoutMs,
                                          uint8_t PrintCommand)
{
	uint16_t CommandLength;
	uint16_t ResponseLength = 0U;
	uint16_t Index;
	uint32_t ElapsedMs = 0U;
	uint8_t Data;

	if (Size == 0U)
	{
		return 0U;
	}

	Serial_ClearRxBuffer(USART2);
	Response[0] = '\0';
	CommandLength = (uint16_t)strlen(Command);
	if (PrintCommand != 0U)
	{
		ESP8266_PrintBytes("ESP8266 tx bytes", (const uint8_t *)Command, CommandLength);
	}

	for (Index = 0; Index < CommandLength; Index++)
	{
		Serial_SendCharBlocking(USART2, (uint8_t)Command[Index]);
		while (Serial_ReadByteNonBlocking(USART2, &Data) != 0U)
		{
			ESP8266_AppendByte(Response, Size, &ResponseLength, Data);
		}
	}

	Serial_WaitTxComplete(USART2);

	while (ElapsedMs < TimeoutMs)
	{
		while (Serial_ReadByteNonBlocking(USART2, &Data) != 0U)
		{
			ESP8266_AppendByte(Response, Size, &ResponseLength, Data);
			if ((Expected != NULL) && (strstr(Response, Expected) != NULL))
			{
				ESP8266_LastResponseLength = ResponseLength;
				return 1U;
			}
		}

		Delay_ms(1U);
		ElapsedMs++;
	}

	ESP8266_LastResponseLength = ResponseLength;
	if (Expected == NULL)
	{
		return (ResponseLength != 0U) ? 1U : 0U;
	}

	return (strstr(Response, Expected) != NULL) ? 1U : 0U;
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

static uint8_t ESP8266_ProbeBaudRate(uint32_t BaudRate, char *Response, uint16_t Size)
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

static uint8_t ESP8266_SetBaudRate(uint32_t TargetBaudRate, char *Response, uint16_t Size)
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

void ESP8266_Init(uint32_t BaudRate)
{
	Serial_Init(USART2, BaudRate);
	ESP8266_BaudRate = BaudRate;
}

uint8_t ESP8266_RunStartupTest(char *Response, uint16_t Size)
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

uint8_t ESP8266_EnsureBaudRate(uint32_t TargetBaudRate, char *Response, uint16_t Size)
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

uint8_t ESP8266_CheckAlive(char *Response, uint16_t Size)
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

uint8_t ESP8266_DisableEcho(char *Response, uint16_t Size)
{
	return ESP8266_SendCommandEx("ATE0\r\n", "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS);
}

uint8_t ESP8266_SetWiFiModeStation(char *Response, uint16_t Size)
{
	return ESP8266_SendCommandEx("AT+CWMODE=1\r\n", "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS);
}

uint8_t ESP8266_JoinAP(const char *Ssid, const char *Password, char *Response, uint16_t Size)
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

uint8_t ESP8266_QueryIP(char *Response, uint16_t Size)
{
	return ESP8266_SendCommandEx("AT+CIFSR\r\n", "OK", Response, Size, ESP8266_WIFI_QUERY_TIMEOUT_MS);
}

uint8_t ESP8266_SetMultipleConnections(char *Response, uint16_t Size)
{
	return ESP8266_SendCommandEx("AT+CIPMUX=1\r\n", "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS);
}

uint8_t ESP8266_StartTCPServer(uint16_t Port, char *Response, uint16_t Size)
{
	char Command[32];

	sprintf(Command, "AT+CIPSERVER=1,%u\r\n", (unsigned int)Port);
	return ESP8266_SendCommandEx(Command, "OK", Response, Size, ESP8266_SHORT_TIMEOUT_MS);
}

uint8_t ESP8266_SendTCPData(uint8_t LinkId, const uint8_t *Data, uint16_t Length, char *Response, uint16_t Size)
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

uint8_t ESP8266_MQTTUserConfig(uint8_t LinkId, const char *ClientId, const char *Username, const char *Password, char *Response, uint16_t Size)
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

uint8_t ESP8266_MQTTConnect(uint8_t LinkId, const char *Host, uint16_t Port, char *Response, uint16_t Size)
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

uint8_t ESP8266_MQTTSubscribe(uint8_t LinkId, const char *Topic, uint8_t QoS, char *Response, uint16_t Size)
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

uint8_t ESP8266_MQTTClean(uint8_t LinkId, char *Response, uint16_t Size)
{
	char Command[32];

	sprintf(Command, "AT+MQTTCLEAN=%u\r\n", (unsigned int)LinkId);
	return ESP8266_SendCommandEx(Command, "OK", Response, Size, ESP8266_MQTT_TIMEOUT_MS);
}

uint8_t ESP8266_MQTTPublish(uint8_t LinkId, const char *Topic, const char *Payload, uint8_t QoS, uint8_t Retain, char *Response, uint16_t Size)
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

uint32_t ESP8266_GetBaudRate(void)
{
	return ESP8266_BaudRate;
}

uint16_t ESP8266_GetLastResponseLength(void)
{
	return ESP8266_LastResponseLength;
}

void ESP8266_PrintResponseHex(const char *Buffer, uint16_t Length)
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
