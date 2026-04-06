#ifndef __APP_TASKS_H
#define __APP_TASKS_H

#include <stdint.h>
#include "MQTTX.H"

#define APP_TASKS_RESPONSE_SIZE         384U
#define APP_TASKS_WIFI_SSID_SIZE        40U
#define APP_TASKS_WIFI_PASSWORD_SIZE    72U
#define APP_TASKS_WIFI_COMMAND_SIZE     128U
#define APP_TASKS_WIFI_COMMAND_IDLE_MS  200U

typedef struct
{
	uint32_t CommandIdleElapsedMs;
	uint32_t TelemetryElapsedMs;
	uint32_t DisplayElapsedMs;
	uint16_t CommandLength;
	uint8_t MqttReady;
	uint8_t LedState;
	char Response[APP_TASKS_RESPONSE_SIZE];
	char WiFiCommand[APP_TASKS_WIFI_COMMAND_SIZE];
	char Ssid[APP_TASKS_WIFI_SSID_SIZE];
	char Password[APP_TASKS_WIFI_PASSWORD_SIZE];
	MQTTX_Context MQTTContext;
} AppTasks_Context;

typedef struct
{
	STH30_DataTypeDef Climate;
	uint16_t Lux;
	uint8_t Valid;
} AppTasks_SensorSample;

typedef struct
{
	char Command[APP_TASKS_WIFI_COMMAND_SIZE];
} AppTasks_WiFiCommand;

void AppTasks_InitContext(AppTasks_Context *Context);
void AppTasks_RunBootAutoConnect(AppTasks_Context *Context, const char *Ssid, const char *Password);
void AppTasks_CommandStep(AppTasks_Context *Context);
void AppTasks_LinkStep(AppTasks_Context *Context);
void AppTasks_TelemetryStep(AppTasks_Context *Context);
void AppTasks_DisplayStep(AppTasks_Context *Context);
void AppTasks_Tick1ms(AppTasks_Context *Context);

#endif
