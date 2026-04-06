#ifndef __ESP8266_H
#define __ESP8266_H

#include <stdint.h>

void ESP8266_Init(uint32_t BaudRate);
uint8_t ESP8266_SendCommand(const char *Command, const char *Expected, char *Response, uint16_t Size);
uint8_t ESP8266_SendCommandEx(const char *Command, const char *Expected, char *Response, uint16_t Size, uint32_t TimeoutMs);
uint8_t ESP8266_RunStartupTest(char *Response, uint16_t Size);
uint8_t ESP8266_EnsureBaudRate(uint32_t TargetBaudRate, char *Response, uint16_t Size);
uint8_t ESP8266_CheckAlive(char *Response, uint16_t Size);
uint8_t ESP8266_DisableEcho(char *Response, uint16_t Size);
uint8_t ESP8266_SetWiFiModeStation(char *Response, uint16_t Size);
uint8_t ESP8266_JoinAP(const char *Ssid, const char *Password, char *Response, uint16_t Size);
uint8_t ESP8266_QueryIP(char *Response, uint16_t Size);

uint8_t ESP8266_SetMultipleConnections(char *Response, uint16_t Size);
uint8_t ESP8266_StartTCPServer(uint16_t Port, char *Response, uint16_t Size);
uint8_t ESP8266_SendTCPData(uint8_t LinkId, const uint8_t *Data, uint16_t Length, char *Response, uint16_t Size);

uint8_t ESP8266_MQTTUserConfig(uint8_t LinkId, const char *ClientId, const char *Username, const char *Password, char *Response, uint16_t Size);
uint8_t ESP8266_MQTTConnect(uint8_t LinkId, const char *Host, uint16_t Port, char *Response, uint16_t Size);
uint8_t ESP8266_MQTTSubscribe(uint8_t LinkId, const char *Topic, uint8_t QoS, char *Response, uint16_t Size);
uint8_t ESP8266_MQTTPublish(uint8_t LinkId, const char *Topic, const char *Payload, uint8_t QoS, uint8_t Retain, char *Response, uint16_t Size);
uint8_t ESP8266_MQTTClean(uint8_t LinkId, char *Response, uint16_t Size);

uint32_t ESP8266_GetBaudRate(void);
uint16_t ESP8266_GetLastResponseLength(void);
void ESP8266_PrintResponseHex(const char *Buffer, uint16_t Length);

#endif
