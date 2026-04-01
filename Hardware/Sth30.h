#ifndef __STH30_H /* 防止 STH30 头文件被重复包含。 */
#define __STH30_H /* 定义 STH30 头文件保护宏。 */

#include "stm32f10x.h" /* 包含 STM32 通用类型定义。 */

typedef struct /* 定义保存温湿度结果的数据结构。 */
{
	int16_t Temperature; /* 保存放大 10 倍后的温度值。 */
	uint16_t Humidity; /* 保存放大 10 倍后的湿度值。 */
} STH30_DataTypeDef; /* 为温湿度数据结构定义类型名。 */

void STH30_Init(void); /* 声明 STH30 初始化函数。 */
uint8_t STH30_ReadData(STH30_DataTypeDef *Data); /* 声明 STH30 温湿度读取函数。 */

#endif /* 结束 STH30 头文件保护。 */
