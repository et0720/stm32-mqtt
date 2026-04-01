#ifndef __LED_H
#define __LED_H

void LED_Init(void); /* 初始化板载 LED。 */
void LED1_ON(void); /* 点亮低电平有效的 LED1。 */
void LED1_OFF(void); /* 熄灭 LED1。 */
void LED1_Turn(void); /* 翻转 LED1 当前状态。 */
void LED2_ON(void); /* 预留接口，当前硬件未实现 LED2。 */
void LED2_OFF(void); /* 预留接口，当前硬件未实现 LED2。 */
void LED2_Turn(void); /* 预留接口，当前硬件未实现 LED2。 */

#endif
