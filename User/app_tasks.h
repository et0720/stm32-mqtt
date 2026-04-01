#ifndef __APP_TASKS_H
#define __APP_TASKS_H

#include <stdint.h>
#include "MQTTX.H"

/* 应用层公共缓冲区配置。 */
#define APP_TASKS_RESPONSE_SIZE         384U
#define APP_TASKS_WIFI_SSID_SIZE        40U
#define APP_TASKS_WIFI_PASSWORD_SIZE    72U
#define APP_TASKS_WIFI_COMMAND_SIZE     128U
#define APP_TASKS_WIFI_COMMAND_IDLE_MS  200U

/* 裸机轮询模式与 RTOS 链路任务共用的应用上下文。 */
typedef struct
{
	uint32_t CommandIdleElapsedMs; /* 串口命令空闲自动提交计时。 */
	uint32_t TelemetryElapsedMs; /* 周期遥测上报计时。 */
	uint32_t DisplayElapsedMs; /* 预留给独立显示任务的刷新节拍。 */
	uint16_t CommandLength; /* 当前正在组装的串口命令长度。 */
	uint8_t MqttReady; /* 当前 MQTT 会话是否可用。 */
	uint8_t LedState; /* 业务层维护的 LED 目标状态。 */
	char Response[APP_TASKS_RESPONSE_SIZE]; /* ESP8266 最近一次命令响应缓冲区。 */
	char WiFiCommand[APP_TASKS_WIFI_COMMAND_SIZE]; /* USART1 组装中的命令缓存。 */
	char Ssid[APP_TASKS_WIFI_SSID_SIZE]; /* 当前生效的 Wi-Fi SSID。 */
	char Password[APP_TASKS_WIFI_PASSWORD_SIZE]; /* 当前生效的 Wi-Fi 密码。 */
	MQTTX_Context MQTTContext; /* MQTT 下行消息解析状态机。 */
} AppTasks_Context;

/* 一帧环境遥测数据。 */
typedef struct
{
	STH30_DataTypeDef Climate; /* 温湿度原始读数。 */
	uint16_t Lux; /* 光照读数。 */
	uint8_t Valid; /* 当前样本是否有效。 */
} AppTasks_SensorSample;

/* 通过 USART1 下发的联网命令。 */
typedef struct
{
	char Command[APP_TASKS_WIFI_COMMAND_SIZE];
} AppTasks_WiFiCommand;

/* 裸机与 RTOS 共享的应用层入口。 */
void AppTasks_InitContext(AppTasks_Context *Context);
void AppTasks_RunBootAutoConnect(AppTasks_Context *Context, const char *Ssid, const char *Password);
void AppTasks_CommandStep(AppTasks_Context *Context);
void AppTasks_LinkStep(AppTasks_Context *Context);
void AppTasks_TelemetryStep(AppTasks_Context *Context);
void AppTasks_DisplayStep(AppTasks_Context *Context);
void AppTasks_Tick1ms(AppTasks_Context *Context);

#endif
