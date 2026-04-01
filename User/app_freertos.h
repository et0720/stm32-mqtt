#ifndef __APP_FREERTOS_H
#define __APP_FREERTOS_H

#include <stdint.h>
#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* 创建 FreeRTOS 资源和业务任务。真正启动调度器仍由 main() 负责。 */
uint8_t AppFreeRTOS_Start(void);

/* 线程安全的日志输出接口。 */
int AppRTOS_LogPrintf(const char *Format, ...);

/* OLED 访问在 RTOS 模式下需要互斥保护。 */
void AppRTOS_OLEDLock(void);
void AppRTOS_OLEDUnlock(void);
void AppRTOS_OLEDClear(void);
void AppRTOS_OLEDShowLine(uint8_t Line, const char *Text);

/* 暴露 ESP8266 命令互斥量，供跨模块串行化访问。 */
SemaphoreHandle_t AppRTOS_GetESPCommandMutex(void);

/* 串口中断里用于唤醒对应任务的桥接入口。 */
BaseType_t AppRTOS_GiveRxSemaphoreFromISR(USART_TypeDef *USARTx);

#endif
