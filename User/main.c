#include "stm32f10x.h"
#include "misc.h"
#include "Delay.h"
#include "LED.h"
#include "OLED.h"
#include "esp8266.h"
#include "app_tasks.h"
#include "MQTTX.H"
#include "serial.h"
#include "app_config.h"
#if (APP_FREERTOS_ENABLE != 0U)
#include "FreeRTOS.h"
#include "task.h"
#include "app_freertos.h"
#endif
#include <stdio.h>
#include <string.h>

#if (APP_FREERTOS_ENABLE == 0U)
static AppTasks_Context MainTasks;
#endif

static void OLED_ShowLine(uint8_t Line, const char *Text)
{
	OLED_ShowString(Line, 1, "                ");
	OLED_ShowString(Line, 1, (char *)Text);
}

#if (APP_USART1_ENABLE == 0U)
static void Main_ConfigUSART1PinsIdle(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}
#endif

/* 根据当前编译配置显示启动页，避免公开仓库默认配置造成误导。 */
static void Main_ShowBootScreen(void)
{
	OLED_Clear();
	OLED_ShowLine(2, "MQTT EDGE");

#if (APP_WIFI_AUTO_CONNECT_ENABLE != 0U)
	char OledLine[17];

	OLED_ShowLine(1, "AUTO WIFI");
	sprintf(OledLine, "SSID:%s", APP_WIFI_AUTO_SSID);
	OLED_ShowLine(3, OledLine);
#else
	OLED_ShowLine(1, "MANUAL WIFI");
#if (APP_USART1_ENABLE != 0U)
	OLED_ShowLine(3, "U1:WIFI:S,P");
#else
	OLED_ShowLine(3, "SET WIFI CFG");
#endif
#endif

#if (APP_FREERTOS_ENABLE != 0U)
	OLED_ShowLine(4, "RTOS START");
#elif (APP_WIFI_AUTO_CONNECT_ENABLE != 0U)
	OLED_ShowLine(4, "BOOT WAIT");
#else
	OLED_ShowLine(4, "WAIT CMD");
#endif
}

int main(void)
{
	/* FreeRTOS 在 Cortex-M3 上要求已实现的优先级位全部用于抢占优先级。 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

#if (APP_USART1_ENABLE != 0U)
	Serial_Init(USART1, APP_DEBUG_BAUD_RATE);
	Serial_SetPrintfUSART(USART1);
#else
	Main_ConfigUSART1PinsIdle();
	Serial_SetPrintfUSART(0);
#endif
	ESP8266_Init(APP_ESP_BAUD_RATE);
	OLED_Init();
	LED_Init();
	SystemCoreClockUpdate();
	LED1_OFF();

	Main_ShowBootScreen();

	printf("\r\nSTM32 MQTT edge node start\r\n");
#if (APP_FREERTOS_ENABLE != 0U)
	printf("[BOOT] FreeRTOS enabled\r\n");
#else
	printf("[BOOT] bare-metal scheduler\r\n");
#endif
#if (APP_VERBOSE_BOOT_LOG != 0U)
	printf("SYSCLK=%lu PCLK1=%lu PCLK2=%lu\r\n",
	       SystemCoreClock,
	       Serial_GetPeripheralClock(USART2),
#if (APP_USART1_ENABLE != 0U)
	       Serial_GetPeripheralClock(USART1));
	printf("USART1 debug baud: %lu\r\n", APP_DEBUG_BAUD_RATE);
	printf("USART1 BRR: 0x%04X\r\n", Serial_GetBRR(USART1));
#else
	       0U);
	printf("USART1 disabled\r\n");
#endif
	printf("USART2 esp baud: %lu\r\n", APP_ESP_BAUD_RATE);
	printf("USART2 BRR: 0x%04X\r\n", Serial_GetBRR(USART2));
	printf("ESP RX <- PA2, ESP TX -> PA3\r\n");
#if (APP_USART1_ENABLE != 0U)
	printf("Send on USART1: WIFI:ssid,password\r\n");
	printf("Send on USART1: ATTEST:AT+GMR\r\n");
#endif
#if (APP_WIFI_AUTO_CONNECT_ENABLE != 0U)
	printf("Auto WiFi: %s\r\n", APP_WIFI_AUTO_SSID);
#else
	printf("Auto WiFi: disabled\r\n");
#endif
	printf("MQTT broker: %s:%u\r\n", MQTTX_BROKER_HOST, MQTTX_BROKER_PORT);
	printf("MQTT client: %s\r\n", MQTTX_CLIENT_ID);
	printf("MQTT sub: %s\r\n", MQTTX_TOPIC_CMD);
	printf("MQTT pub: %s\r\n", MQTTX_TOPIC_TELEMETRY);
#endif

#if (APP_WIFI_AUTO_CONNECT_ENABLE == 0U)
#if (APP_USART1_ENABLE != 0U)
	printf("[BOOT] wait manual WIFI:ssid,password on USART1\r\n");
#else
	printf("[BOOT] manual WiFi disabled by config\r\n");
#endif
#endif

#if (APP_FREERTOS_ENABLE != 0U)
	/* 从这里开始，业务逻辑转入 RTOS 任务中执行，不再留在 main() 里轮询。 */
	/* 启动顺序是：
	 * 1. 先调用 AppFreeRTOS_Start() 创建 RTOS 资源和业务任务；
	 * 2. 任务创建成功后，再调用 vTaskStartScheduler() 启动调度器；
	 * 3. 调度器启动后，main() 不再承担业务循环，只保留异常兜底路径。
	 */
	if (AppFreeRTOS_Start() == 0U)
	{
		printf("[BOOT] task create failed\r\n");
		OLED_ShowLine(1, "RTOS ERROR");
		OLED_ShowLine(2, "TASK CREATE");
		OLED_ShowLine(3, "FAILED");
		while (1)
		{
		}
	}

	printf("[BOOT] start scheduler\r\n");
	/* vTaskStartScheduler() 之后由 FreeRTOS 统一调度，上面创建的任务会在这里开始运行。
	 * 注意：任务虽然在 AppFreeRTOS_Start() 里就被创建出来了，但只有调度器启动后它们才会被真正切换执行。
	 */
	vTaskStartScheduler();
	printf("[BOOT] scheduler returned\r\n");
	OLED_ShowLine(1, "RTOS ERROR");
	OLED_ShowLine(2, "SCHED EXIT");
	OLED_ShowLine(3, "CHECK HEAP");
	while (1)
	{
	}
#else
	AppTasks_InitContext(&MainTasks);

#if (APP_WIFI_AUTO_CONNECT_ENABLE != 0U)
	char OledLine[17];

	printf("[BOOT] wait ESP settle %u ms\r\n", (unsigned int)APP_WIFI_AUTO_START_DELAY_MS);
	OLED_ShowLine(4, "ESP SETTLE");
	Delay_ms(APP_WIFI_AUTO_START_DELAY_MS);
	printf("[BOOT] auto connect ssid=%s\r\n", APP_WIFI_AUTO_SSID);
	OLED_ShowLine(1, "AUTO CONNECT");
	sprintf(OledLine, "SSID:%s", APP_WIFI_AUTO_SSID);
	OLED_ShowLine(2, OledLine);
	OLED_ShowLine(3, "WIFI START");
	OLED_ShowLine(4, "PLEASE WAIT");
	AppTasks_RunBootAutoConnect(&MainTasks, APP_WIFI_AUTO_SSID, APP_WIFI_AUTO_PASSWORD);
#endif

	while (1)
	{
#if (APP_USART1_ENABLE != 0U)
		AppTasks_CommandStep(&MainTasks);
#endif
		AppTasks_LinkStep(&MainTasks);
		AppTasks_TelemetryStep(&MainTasks);
		AppTasks_DisplayStep(&MainTasks);

		Delay_ms(1U);
		AppTasks_Tick1ms(&MainTasks);
	}
#endif
}
