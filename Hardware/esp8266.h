#ifndef __ESP8266_H /* 防止 ESP8266 驱动头文件被重复包含。 */
#define __ESP8266_H /* 定义 ESP8266 驱动头文件保护宏。 */

#include <stdint.h> /* 包含标准整型定义。 */

/* 串口与基础 AT 指令。 */
void ESP8266_Init(uint32_t BaudRate); /* 初始化 USART2 与 ESP8266 的串口参数。 */
uint8_t ESP8266_SendCommand(const char *Command, const char *Expected, char *Response, uint16_t Size); /* 发送默认超时 AT 命令。 */
uint8_t ESP8266_SendCommandEx(const char *Command, const char *Expected, char *Response, uint16_t Size, uint32_t TimeoutMs); /* 发送带自定义超时的 AT 命令。 */
uint8_t ESP8266_RunStartupTest(char *Response, uint16_t Size); /* 依次执行常用启动指令，验证模组基础状态。 */
uint8_t ESP8266_EnsureBaudRate(uint32_t TargetBaudRate, char *Response, uint16_t Size); /* 自动探测并切换到工程目标波特率。 */
uint8_t ESP8266_CheckAlive(char *Response, uint16_t Size); /* 发送 AT 检查模组是否在线。 */
uint8_t ESP8266_DisableEcho(char *Response, uint16_t Size); /* 关闭 ESP 串口命令回显。 */
uint8_t ESP8266_SetWiFiModeStation(char *Response, uint16_t Size); /* 设置 ESP 工作在 STA 模式。 */
uint8_t ESP8266_JoinAP(const char *Ssid, const char *Password, char *Response, uint16_t Size); /* 连接指定 WiFi。 */
uint8_t ESP8266_QueryIP(char *Response, uint16_t Size); /* 查询当前 STA IP 地址。 */

/* TCP 场景下的扩展接口，当前 MQTT 方案未直接使用。 */
uint8_t ESP8266_SetMultipleConnections(char *Response, uint16_t Size); /* 配置多连接模式。 */
uint8_t ESP8266_StartTCPServer(uint16_t Port, char *Response, uint16_t Size); /* 启动 TCP Server。 */
uint8_t ESP8266_SendTCPData(uint8_t LinkId, const uint8_t *Data, uint16_t Length, char *Response, uint16_t Size); /* 通过指定链路发送 TCP 数据。 */

/* MQTT AT 指令封装。 */
uint8_t ESP8266_MQTTUserConfig(uint8_t LinkId, const char *ClientId, const char *Username, const char *Password, char *Response, uint16_t Size); /* 配置 MQTT Client ID 和认证参数。 */
uint8_t ESP8266_MQTTConnect(uint8_t LinkId, const char *Host, uint16_t Port, char *Response, uint16_t Size); /* 连接指定 MQTT Broker。 */
uint8_t ESP8266_MQTTSubscribe(uint8_t LinkId, const char *Topic, uint8_t QoS, char *Response, uint16_t Size); /* 订阅 MQTT 主题。 */
uint8_t ESP8266_MQTTPublish(uint8_t LinkId, const char *Topic, const char *Payload, uint8_t QoS, uint8_t Retain, char *Response, uint16_t Size); /* 发布 MQTT 消息。 */
uint8_t ESP8266_MQTTClean(uint8_t LinkId, char *Response, uint16_t Size); /* 清理旧的 MQTT 会话。 */

/* 调试与状态观测接口。 */
uint32_t ESP8266_GetBaudRate(void); /* 获取驱动层当前记录的 ESP 串口波特率。 */
uint16_t ESP8266_GetLastResponseLength(void); /* 获取最近一次响应的字节长度。 */
void ESP8266_PrintResponseHex(const char *Buffer, uint16_t Length); /* 打印最近响应的十六进制字节。 */

#endif /* 结束 ESP8266 驱动头文件保护。 */
