#ifndef __BH1750_H /* 防止 BH1750 头文件被重复包含。 */
#define __BH1750_H /* 定义 BH1750 头文件保护宏。 */

#include "stm32f10x.h" /* 包含 STM32 通用类型定义。 */

void BH1750_Init(void); /* 声明 BH1750 初始化函数。 */
uint8_t BH1750_ReadLux(uint16_t *Lux); /* 声明 BH1750 光照读取函数。 */

#endif /* 结束 BH1750 头文件保护。 */
