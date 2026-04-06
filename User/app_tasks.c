#include "app_tasks.h"
#include "Delay.h"
#include "OLED.h"
#include "esp8266.h"
#include "serial.h"
#include "MQTTX.H"
#include "BH1750.h"
#include "Sth30.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

#if (APP_USART1_ENABLE != 0U)
static uint8_t AppTasks_IsAsciiSpace(uint8_t Data)
{
	return ((Data == ' ') || (Data == '\t')) ? 1U : 0U;
}

static uint8_t AppTasks_IsAsciiLetterEqualIgnoreCase(uint8_t Data, char Letter)
{
	if ((Data >= 'a') && (Data <= 'z'))
	{
		Data = (uint8_t)(Data - ('a' - 'A'));
	}

	return (Data == (uint8_t)Letter) ? 1U : 0U;
}

static uint8_t AppTasks_IsATTestPrefix(const char *Command)
{
	const char *Prefix = "ATTEST:";
	uint8_t Index;

	for (Index = 0U; Prefix[Index] != '\0'; Index++)
	{
		if (Command[Index] == '\0')
		{
			return 0U;
		}

		if (AppTasks_IsAsciiLetterEqualIgnoreCase((uint8_t)Command[Index], Prefix[Index]) == 0U)
		{
			return 0U;
		}
	}

	return 1U;
}

static void AppTasks_PrintResponseOrEmpty(const char *Response)
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

static uint8_t AppTasks_ExecuteATTestCommand(const char *Command, char *Response, uint16_t ResponseSize)
{
	char ForwardCommand[192];
	uint16_t Length;

	while (AppTasks_IsAsciiSpace((uint8_t)*Command) != 0U)
	{
		Command++;
	}

	if (AppTasks_IsATTestPrefix(Command) == 0U)
	{
		return 0U;
	}

	Command += 7;
	while (AppTasks_IsAsciiSpace((uint8_t)*Command) != 0U)
	{
		Command++;
	}

	Length = (uint16_t)strlen(Command);
	while ((Length > 0U) &&
	       ((Command[Length - 1U] == '\r') ||
	        (Command[Length - 1U] == '\n') ||
	        (AppTasks_IsAsciiSpace((uint8_t)Command[Length - 1U]) != 0U)))
	{
		Length--;
	}

	if ((Length == 0U) || (Length >= (uint16_t)(sizeof(ForwardCommand) - 2U)))
	{
		printf("[ATTEST] empty command\r\n");
		return 1U;
	}

	memcpy(ForwardCommand, Command, Length);
	ForwardCommand[Length++] = '\r';
	ForwardCommand[Length++] = '\n';
	ForwardCommand[Length] = '\0';

	printf("[ATTEST] -> %s", ForwardCommand);
	if (ESP8266_SendCommandEx(ForwardCommand, NULL, Response, ResponseSize, 5000U) != 0U)
	{
		printf("[ATTEST] rx len=%u\r\n", ESP8266_GetLastResponseLength());
		AppTasks_PrintResponseOrEmpty(Response);
		return 1U;
	}

	printf("[ATTEST] timeout len=%u\r\n", ESP8266_GetLastResponseLength());
	AppTasks_PrintResponseOrEmpty(Response);
	return 1U;
}
#endif

static void AppTasks_PrintEspAsyncByte(uint8_t Data)
{
#if (APP_USART1_ENABLE == 0U)
	(void)Data;
	return;
#else
	if ((Data == '\r') || (Data == '\n'))
	{
		Serial_SendCharBlocking(USART1, Data);
		return;
	}

	if ((Data >= 32U) && (Data <= 126U))
	{
		Serial_SendCharBlocking(USART1, Data);
	}
	else
	{
		printf("<%02X>", (unsigned int)Data);
	}
#endif
}

void AppTasks_InitContext(AppTasks_Context *Context)
{
	memset(Context, 0, sizeof(*Context));
	STH30_Init();
	BH1750_Init();
	MQTTX_InitContext(&Context->MQTTContext);
}

void AppTasks_RunBootAutoConnect(AppTasks_Context *Context, const char *Ssid, const char *Password)
{
	strcpy(Context->Ssid, Ssid);
	strcpy(Context->Password, Password);
	Context->MqttReady = MQTTX_StartSession(Context->Ssid,
	                                        Context->Password,
	                                        Context->Response,
	                                        sizeof(Context->Response),
	                                        &Context->LedState);
	Context->TelemetryElapsedMs = 0U;
#if (APP_USART1_ENABLE != 0U)
	if (Context->MqttReady == 0U)
	{
		printf("[BOOT] auto connect failed, manual WIFI:ssid,password remains available\r\n");
	}
#endif
}

void AppTasks_CommandStep(AppTasks_Context *Context)
{
#if (APP_USART1_ENABLE == 0U)
	(void)Context;
	return;
#else
	uint8_t PcData;

	if (Serial_ReadByteNonBlocking(USART1, &PcData) != 0U)
	{
		Context->CommandIdleElapsedMs = 0U;
		if ((PcData == '\r') || (PcData == '\n'))
		{
			if (Context->CommandLength != 0U)
			{
				Context->WiFiCommand[Context->CommandLength] = '\0';
				Context->CommandLength = 0U;
				if (AppTasks_ExecuteATTestCommand(Context->WiFiCommand, Context->Response, sizeof(Context->Response)) != 0U)
				{
					return;
				}

				MQTTX_InitContext(&Context->MQTTContext);
				Context->MqttReady = MQTTX_ExecuteWiFiCommand(Context->WiFiCommand,
				                                              Context->Ssid,
				                                              sizeof(Context->Ssid),
				                                              Context->Password,
				                                              sizeof(Context->Password),
				                                              Context->Response,
				                                              sizeof(Context->Response),
				                                              &Context->LedState);
				Context->TelemetryElapsedMs = 0U;
			}
		}
		else if ((PcData == 0x08U) || (PcData == 0x7FU))
		{
			if (Context->CommandLength != 0U)
			{
				Context->CommandLength--;
			}
		}
		else if (Context->CommandLength < (APP_TASKS_WIFI_COMMAND_SIZE - 1U))
		{
			Context->WiFiCommand[Context->CommandLength++] = (char)PcData;
		}
	}
	else if ((Context->CommandLength != 0U) && (Context->CommandIdleElapsedMs >= APP_TASKS_WIFI_COMMAND_IDLE_MS))
	{
		Context->WiFiCommand[Context->CommandLength] = '\0';
		Context->CommandLength = 0U;
		Context->CommandIdleElapsedMs = 0U;
		printf("[CMD] auto-submit on idle\r\n");
		if (AppTasks_ExecuteATTestCommand(Context->WiFiCommand, Context->Response, sizeof(Context->Response)) != 0U)
		{
			return;
		}

		MQTTX_InitContext(&Context->MQTTContext);
		Context->MqttReady = MQTTX_ExecuteWiFiCommand(Context->WiFiCommand,
		                                              Context->Ssid,
		                                              sizeof(Context->Ssid),
		                                              Context->Password,
		                                              sizeof(Context->Password),
		                                              Context->Response,
		                                              sizeof(Context->Response),
		                                              &Context->LedState);
		Context->TelemetryElapsedMs = 0U;
	}
#endif
}

void AppTasks_LinkStep(AppTasks_Context *Context)
{
	uint8_t EspData;

	if (Context->MqttReady == 0U)
	{
		return;
	}

	while (Serial_ReadByteNonBlocking(USART2, &EspData) != 0U)
	{
		AppTasks_PrintEspAsyncByte(EspData);
		MQTTX_ConsumeAsyncByte(&Context->MQTTContext,
		                       EspData,
		                       &Context->LedState,
		                       Context->Response,
		                       sizeof(Context->Response));
	}
}

void AppTasks_TelemetryStep(AppTasks_Context *Context)
{
	if ((Context->MqttReady != 0U) && (Context->TelemetryElapsedMs >= MQTTX_TELEMETRY_INTERVAL_MS))
	{
		MQTTX_PublishTelemetry(Context->LedState, Context->Response, sizeof(Context->Response));
		Context->TelemetryElapsedMs = 0U;
	}
}

void AppTasks_DisplayStep(AppTasks_Context *Context)
{
	(void)Context;
}

void AppTasks_Tick1ms(AppTasks_Context *Context)
{
	if (Context->CommandIdleElapsedMs < APP_TASKS_WIFI_COMMAND_IDLE_MS)
	{
		Context->CommandIdleElapsedMs++;
	}
	if (Context->TelemetryElapsedMs < MQTTX_TELEMETRY_INTERVAL_MS)
	{
		Context->TelemetryElapsedMs++;
	}
	if (Context->DisplayElapsedMs < 1000U)
	{
		Context->DisplayElapsedMs++;
	}
}
