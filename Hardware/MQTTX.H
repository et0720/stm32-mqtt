#ifndef __MQTTX_H
#define __MQTTX_H

#include <stdint.h>
#include "Sth30.h"

/* Public broker / topic defaults. Update them before deploying your own node. */
#define MQTTX_LINK_ID                 0U
#define MQTTX_BROKER_HOST             "broker.emqx.io"
#define MQTTX_BROKER_PORT             1883U
#define MQTTX_CLIENT_ID               "lol-edge-001"
#define MQTTX_TOPIC_STATUS            "lol/edge/001/status"
#define MQTTX_TOPIC_TELEMETRY         "lol/edge/001/telemetry"
#define MQTTX_TOPIC_CMD               "lol/edge/001/cmd"
#define MQTTX_TELEMETRY_INTERVAL_MS   5000U

typedef enum
{
	MQTTX_SUB_IDLE = 0,
	MQTTX_SUB_HEADER,
	MQTTX_SUB_PAYLOAD
} MQTTX_SubState;

typedef struct
{
	MQTTX_SubState State; /* 当前下行解析状态。 */
	char Header[96]; /* 暂存 +MQTTSUBRECV 头部。 */
	uint16_t HeaderLength; /* 头部已接收字节数。 */
	char Topic[64]; /* 当前下行消息主题。 */
	char Payload[160]; /* 当前下行消息 payload。 */
	uint16_t PayloadExpectedLength; /* 头部声明的 payload 长度。 */
	uint16_t PayloadReceivedLength; /* 实际已经接收到的 payload 长度。 */
	uint16_t PayloadStoredLength; /* 真实存入缓冲区的 payload 长度。 */
	uint8_t LinkId; /* 当前 MQTT 链路编号。 */
	uint8_t InQuotes; /* 头部解析时是否位于引号内。 */
	uint8_t CommaCount; /* 头部解析时已统计到的字段分隔逗号数量。 */
} MQTTX_Context;

/* Wi-Fi + MQTT 会话控制接口。 */
void MQTTX_InitContext(MQTTX_Context *Context);
uint8_t MQTTX_Connect(char *Response, uint16_t Size, const char *StaIp);
uint8_t MQTTX_StartSession(const char *Ssid, const char *Password, char *Response, uint16_t ResponseSize, uint8_t *LedState);
uint8_t MQTTX_ExecuteWiFiCommand(const char *WiFiCommand,
                                 char *Ssid,
                                 uint16_t SsidSize,
                                 char *Password,
                                 uint16_t PasswordSize,
                                 char *Response,
                                 uint16_t ResponseSize,
                                 uint8_t *LedState);
uint8_t MQTTX_PublishStatus(uint8_t LedState, char *Response, uint16_t Size);
uint8_t MQTTX_PublishTelemetry(uint8_t LedState, char *Response, uint16_t Size);

/* 按字节驱动 MQTT 下行解析状态机。 */
void MQTTX_ConsumeAsyncByte(MQTTX_Context *Context, uint8_t Data, uint8_t *LedState, char *Response, uint16_t ResponseSize);

#endif
