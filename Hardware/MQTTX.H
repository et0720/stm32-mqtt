#ifndef __MQTTX_H
#define __MQTTX_H

#include <stdint.h>
#include "Sth30.h"

/* Public broker/topic defaults. Update them before deploying your own node. */
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

/*
 * PayloadReceivedLength tracks the broker-declared payload size.
 * PayloadStoredLength tracks the truncated local copy in Payload[] so the
 * parser can drain oversized frames without overrunning the buffer.
 */
typedef struct
{
	MQTTX_SubState State;
	char Header[96];
	uint16_t HeaderLength;
	char Topic[64];
	char Payload[160];
	uint16_t PayloadExpectedLength;
	uint16_t PayloadReceivedLength;
	uint16_t PayloadStoredLength;
	uint8_t LinkId;
	uint8_t InQuotes;
	uint8_t CommaCount;
} MQTTX_Context;

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

/* Byte-wise parser for ESP-AT +MQTTSUBRECV notifications. */
void MQTTX_ConsumeAsyncByte(MQTTX_Context *Context, uint8_t Data, uint8_t *LedState, char *Response, uint16_t ResponseSize);

#endif
