#ifndef __OLED_H /* 防止 OLED 头文件被重复包含。 */
#define __OLED_H /* 定义 OLED 头文件保护宏。 */

void OLED_Init(void); /* 声明 OLED 初始化函数。 */
void OLED_Clear(void); /* 声明 OLED 清屏函数。 */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char); /* 声明 OLED 单字符显示函数。 */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String); /* 声明 OLED 字符串显示函数。 */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length); /* 声明 OLED 无符号数字显示函数。 */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length); /* 声明 OLED 有符号数字显示函数。 */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length); /* 声明 OLED 十六进制显示函数。 */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length); /* 声明 OLED 二进制显示函数。 */

#endif /* 结束 OLED 头文件保护。 */
