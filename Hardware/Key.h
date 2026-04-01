#ifndef __KEY_H
#define __KEY_H

#include <stdint.h>

void Key_Init(void); /* 初始化 PB1 / PB11 按键输入。 */
uint8_t Key_GetNum(void); /* 获取按键编号，未按下时返回 0。 */

#endif
