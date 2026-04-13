#include "app_freertos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "app_config.h"
#include "app_tasks.h"
#include "OLED.h"
#include "esp8266.h"
#include "serial.h"
#include "BH1750.h"
#include "Sth30.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* 链接任务拥有 ESP8266 的访问权，因此它获得最高优先级和最大的堆栈。 */
#define APP_RTOS_LINK_TASK_STACK_WORDS       512U
#define APP_RTOS_LINK_TASK_PRIORITY          3U
#define APP_RTOS_TELEMETRY_TASK_STACK_WORDS  512U
#define APP_RTOS_TELEMETRY_TASK_PRIORITY     2U
#define APP_RTOS_SENSOR_TASK_STACK_WORDS     256U
#define APP_RTOS_SENSOR_TASK_PRIORITY        2U
#define APP_RTOS_COMMAND_TASK_STACK_WORDS    256U
#define APP_RTOS_COMMAND_TASK_PRIORITY       2U
/* 传感器队列仅保留最新的样本；其他队列仅吸收短暂的突发数据。 */
#define APP_RTOS_SENSOR_QUEUE_LENGTH         1U
#define APP_RTOS_PUBLISH_QUEUE_LENGTH        2U
#define APP_RTOS_WIFI_QUEUE_LENGTH           4U
#define APP_RTOS_SENSOR_PERIOD_MS            MQTTX_TELEMETRY_INTERVAL_MS

typedef struct
{
    uint8_t MqttReady;
    uint8_t LedState;
} AppRTOS_SharedState;

static AppTasks_Context AppLinkContext;
static AppRTOS_SharedState AppSharedState;
static QueueHandle_t AppSensorQueue = NULL;
static QueueHandle_t AppPublishQueue = NULL;
static QueueHandle_t AppWiFiCommandQueue = NULL;
static SemaphoreHandle_t AppUsart1RxSemaphore = NULL;
static SemaphoreHandle_t AppUsart2RxSemaphore = NULL;
static SemaphoreHandle_t AppLogMutex = NULL;
static SemaphoreHandle_t AppOLEDMutex = NULL;
static SemaphoreHandle_t AppESPCommandMutex = NULL;
static SemaphoreHandle_t AppStateMutex = NULL;
static volatile uint8_t AppLinkBootReady = 0U;

/* 将带符号的 0.1°C 传感器值转换为显示安全的分数位。 */
static uint16_t AppRTOS_TemperatureFraction(int16_t Temperature)
{
    /* C 语言在 % 运算中保留符号，因此对分数位进行归一化以用于显示/JSON。 */
    return (uint16_t)((Temperature >= 0) ? (Temperature % 10) : (-Temperature % 10));
}

/* 链接任务修改此状态，而遥测/显示路径仅采样快照。 */
static void AppRTOS_SetSharedState(uint8_t MqttReady, uint8_t LedState)
{
    if (AppStateMutex != NULL)
    {
        xSemaphoreTake(AppStateMutex, portMAX_DELAY);
    }

    AppSharedState.MqttReady = MqttReady;
    AppSharedState.LedState = LedState;

    if (AppStateMutex != NULL)
    {
        xSemaphoreGive(AppStateMutex);
    }
}

/* 读取一致的 MQTT 就绪/LED 状态快照，而不向调用者暴露锁的详细信息。 */
static void AppRTOS_GetSharedState(uint8_t *MqttReady, uint8_t *LedState)
{
    if (AppStateMutex != NULL)
    {
        xSemaphoreTake(AppStateMutex, portMAX_DELAY);
    }

    if (MqttReady != 0)
    {
        *MqttReady = AppSharedState.MqttReady;
    }
    if (LedState != 0)
    {
        *LedState = AppSharedState.LedState;
    }

    if (AppStateMutex != NULL)
    {
        xSemaphoreGive(AppStateMutex);
    }
}

/* 为仍使用命令式绘制的模块序列化直接 OLED 访问。 */
void AppRTOS_OLEDLock(void)
{
    if (AppOLEDMutex != NULL)
    {
        xSemaphoreTake(AppOLEDMutex, portMAX_DELAY);
    }
}

/* 释放共享的 OLED/I2C 锁。 */
void AppRTOS_OLEDUnlock(void)
{
    if (AppOLEDMutex != NULL)
    {
        xSemaphoreGive(AppOLEDMutex);
    }
}

/* 通过 RTOS 安全的包装器清除 OLED。 */
void AppRTOS_OLEDClear(void)
{
    AppRTOS_OLEDLock();
    OLED_Clear();
    AppRTOS_OLEDUnlock();
}

/* 通过 RTOS 安全的包装器重写一行 OLED。 */
void AppRTOS_OLEDShowLine(uint8_t Line, const char *Text)
{
    AppRTOS_OLEDLock();
    OLED_ShowString(Line, 1, "                ");
    OLED_ShowString(Line, 1, (char *)Text);
    AppRTOS_OLEDUnlock();
}

/* 暴露 ESP 命令互斥锁，以便低级代码加入相同的序列化契约。 */
SemaphoreHandle_t AppRTOS_GetESPCommandMutex(void)
{
    return AppESPCommandMutex;
}

/* ISR 仅唤醒等待的任务；所有解析都留在任务上下文中。 */
BaseType_t AppRTOS_GiveRxSemaphoreFromISR(USART_TypeDef *USARTx)
{
    BaseType_t HigherPriorityTaskWoken = pdFALSE;

    if ((USARTx == USART1) && (AppUsart1RxSemaphore != NULL))
    {
        xSemaphoreGiveFromISR(AppUsart1RxSemaphore, &HigherPriorityTaskWoken);
    }
    else if ((USARTx == USART2) && (AppUsart2RxSemaphore != NULL))
    {
        xSemaphoreGiveFromISR(AppUsart2RxSemaphore, &HigherPriorityTaskWoken);
    }

    return HigherPriorityTaskWoken;
}

/* 本地格式化并一次性发送，以便不同任务的日志行不会交错。 */
int AppRTOS_LogPrintf(const char *Format, ...)
{
    char Buffer[256];
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);

    if (Length <= 0)
    {
        return Length;
    }

    if (AppLogMutex != NULL)
    {
        xSemaphoreTake(AppLogMutex, portMAX_DELAY);
    }

#if (APP_USART1_ENABLE != 0U)
    Serial_SendStringBlocking(USART1, Buffer);
#endif

    if (AppLogMutex != NULL)
    {
        xSemaphoreGive(AppLogMutex);
    }

    return Length;
}

/* 当恢复不再有意义时，用硬故障屏幕替换用户界面。 */
static void AppFreeRTOS_ShowFatal(const char *Line1, const char *Line2)
{
    AppRTOS_OLEDLock();
    OLED_Clear();
    OLED_ShowString(1, 1, "RTOS FAIL");
    if (Line1 != 0)
    {
        OLED_ShowString(2, 1, (char *)Line1);
    }
    if (Line2 != 0)
    {
        OLED_ShowString(3, 1, (char *)Line2);
    }
    AppRTOS_OLEDUnlock();
}

/* OLED 和两个传感器共享 I2C1，因此采样必须相对于显示刷新进行序列化。 */
static uint8_t AppRTOS_ReadSensorSample(AppTasks_SensorSample *Sample)
{
    if (Sample == 0)
    {
        return 0U;
    }

    memset(Sample, 0, sizeof(*Sample));

    AppRTOS_OLEDLock();
    if ((STH30_ReadData(&Sample->Climate) != 0U) || (BH1750_ReadLux(&Sample->Lux) != 0U))
    {
        AppRTOS_OLEDUnlock();
        AppRTOS_OLEDShowLine(1, "MQTT ONLINE");
        AppRTOS_OLEDShowLine(2, "SENSOR FAIL");
        AppRTOS_OLEDShowLine(3, "CHECK I2C");
        AppRTOS_OLEDShowLine(4, "PB6 PB7");
        return 0U;
    }
    AppRTOS_OLEDUnlock();

    Sample->Valid = 1U;
    return 1U;
}

/* 将最新的采样值渲染到 OLED 上，而不回溯到传感器驱动程序。 */
static void AppRTOS_ShowSample(const AppTasks_SensorSample *Sample, uint8_t LedState)
{
    char Line[17];
    int16_t Temperature;
    uint16_t TemperatureFraction;

    if ((Sample == 0) || (Sample->Valid == 0U))
    {
        return;
    }

    Temperature = Sample->Climate.Temperature;
    TemperatureFraction = AppRTOS_TemperatureFraction(Temperature);

    AppRTOS_OLEDLock();
    OLED_ShowString(1, 1, "                ");
    OLED_ShowString(1, 1, "MQTT ONLINE");
    sprintf(Line, "T:%d.%uC H:%u%%", Temperature / 10, TemperatureFraction, Sample->Climate.Humidity / 10);
    OLED_ShowString(2, 1, "                ");
    OLED_ShowString(2, 1, Line);
    sprintf(Line, "L:%ulx LED:%s", (unsigned int)Sample->Lux, (LedState != 0U) ? "ON" : "OFF");
    OLED_ShowString(3, 1, "                ");
    OLED_ShowString(3, 1, Line);
    OLED_ShowString(4, 1, "                ");
    OLED_ShowString(4, 1, "CMD READY");
    AppRTOS_OLEDUnlock();
}

/* 发布预采样帧，以便遥测定时与传感器 I/O 延迟解耦。 */
static uint8_t AppRTOS_PublishSample(const AppTasks_SensorSample *Sample, uint8_t LedState, char *Response, uint16_t ResponseSize)
{
    char Payload[160];
    int16_t Temperature;
    uint16_t TemperatureFraction;

    if ((Sample == 0) || (Sample->Valid == 0U))
    {
        return 0U;
    }

    Temperature = Sample->Climate.Temperature;
    TemperatureFraction = AppRTOS_TemperatureFraction(Temperature);
    sprintf(Payload,
            "{\"temp\":%d.%u,\"humi\":%u.%u,\"lux\":%u,\"led\":%u}",
            Temperature / 10,
            TemperatureFraction,
            Sample->Climate.Humidity / 10,
            Sample->Climate.Humidity % 10,
            (unsigned int)Sample->Lux,
            (unsigned int)LedState);

    AppRTOS_LogPrintf("[STEP] MQTTPUB telemetry\r\n");
    return ESP8266_MQTTPublish(MQTTX_LINK_ID, MQTTX_TOPIC_TELEMETRY, Payload, 0U, 0U, Response, ResponseSize);
}

#if (APP_USART1_ENABLE != 0U)
/* 打包当前的 UART 命令行并将其交给链接任务。 */
static uint8_t AppRTOS_SubmitCommand(char *CommandBuffer, uint16_t *CommandLength)
{
    AppTasks_WiFiCommand Command;

    if (*CommandLength == 0U)
    {
        return 0U;
    }

    memcpy(Command.Command, CommandBuffer, *CommandLength);
    Command.Command[*CommandLength] = '\0';
    *CommandLength = 0U;

    /* 手动 Wi-Fi 命令应短暂阻塞，而不是在队列满时消失。 */
    if (xQueueSend(AppWiFiCommandQueue, &Command, portMAX_DELAY) != pdPASS)
    {
        return 0U;
    }

    return 1U;
}

/* 解析 USART1 上的用户输入，并将完整的 Wi-Fi 命令交给链接任务。 */
static void AppFreeRTOS_CommandTask(void *Argument)
{
    char CommandBuffer[APP_TASKS_WIFI_COMMAND_SIZE];
    uint16_t CommandLength = 0U;
    TickType_t LastRxTick = xTaskGetTickCount();

    (void)Argument;
    memset(CommandBuffer, 0, sizeof(CommandBuffer));

    for (;;)
    {
        if (xSemaphoreTake(AppUsart1RxSemaphore, pdMS_TO_TICKS(APP_TASKS_WIFI_COMMAND_IDLE_MS)) == pdTRUE)
        {
            uint8_t PcData;

            while (Serial_ReadByteNonBlocking(USART1, &PcData) != 0U)
            {
                LastRxTick = xTaskGetTickCount();
                if ((PcData == '\r') || (PcData == '\n'))
                {
                    (void)AppRTOS_SubmitCommand(CommandBuffer, &CommandLength);
                }
                else if ((PcData == 0x08U) || (PcData == 0x7FU))
                {
                    if (CommandLength != 0U)
                    {
                        CommandLength--;
                    }
                }
                else if (CommandLength < (APP_TASKS_WIFI_COMMAND_SIZE - 1U))
                {
                    CommandBuffer[CommandLength++] = (char)PcData;
                }
            }
        }
        else if ((CommandLength != 0U) &&
                 ((xTaskGetTickCount() - LastRxTick) >= pdMS_TO_TICKS(APP_TASKS_WIFI_COMMAND_IDLE_MS)))
        {
            AppRTOS_LogPrintf("[CMD] auto-submit on idle\r\n");
            (void)AppRTOS_SubmitCommand(CommandBuffer, &CommandLength);
        }
    }
}
#endif

/* 在此处集中所有 ESP8266 访问，以便命令、发布和 RX 流永远不会相互竞争。 */
static void AppFreeRTOS_LinkTask(void *Argument)
{
    char Response[APP_TASKS_RESPONSE_SIZE];

    (void)Argument;
    AppTasks_InitContext(&AppLinkContext);
    memset(Response, 0, sizeof(Response));
    AppRTOS_SetSharedState(0U, 0U);
    AppLinkBootReady = 0U;

#if (APP_WIFI_AUTO_CONNECT_ENABLE != 0U)
    AppRTOS_LogPrintf("[RTOS] wait ESP settle %u ms\r\n", (unsigned int)APP_WIFI_AUTO_START_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(APP_WIFI_AUTO_START_DELAY_MS));
    xSemaphoreTake(AppESPCommandMutex, portMAX_DELAY);
    AppTasks_RunBootAutoConnect(&AppLinkContext, APP_WIFI_AUTO_SSID, APP_WIFI_AUTO_PASSWORD);
    xSemaphoreGive(AppESPCommandMutex);
    AppRTOS_SetSharedState(AppLinkContext.MqttReady, AppLinkContext.LedState);
#endif
    AppLinkBootReady = 1U;

    for (;;)
    {
        AppTasks_SensorSample PublishSample;

#if (APP_USART1_ENABLE != 0U)
        AppTasks_WiFiCommand Command;

        if (xQueueReceive(AppWiFiCommandQueue, &Command, 0U) == pdPASS)
        {
            xSemaphoreTake(AppESPCommandMutex, portMAX_DELAY);
            MQTTX_InitContext(&AppLinkContext.MQTTContext);
            AppLinkContext.MqttReady = MQTTX_ExecuteWiFiCommand(Command.Command,
                                                                AppLinkContext.Ssid,
                                                                sizeof(AppLinkContext.Ssid),
                                                                AppLinkContext.Password,
                                                                sizeof(AppLinkContext.Password),
                                                                Response,
                                                                sizeof(Response),
                                                                &AppLinkContext.LedState);
            xSemaphoreGive(AppESPCommandMutex);
            AppRTOS_SetSharedState(AppLinkContext.MqttReady, AppLinkContext.LedState);
            continue;
        }
#endif

        if (xQueueReceive(AppPublishQueue, &PublishSample, 0U) == pdPASS)
        {
            xSemaphoreTake(AppESPCommandMutex, portMAX_DELAY);
            if (AppLinkContext.MqttReady != 0U)
            {
                (void)AppRTOS_PublishSample(&PublishSample,
                                            AppLinkContext.LedState,
                                            Response,
                                            sizeof(Response));
            }
            xSemaphoreGive(AppESPCommandMutex);
            AppRTOS_SetSharedState(AppLinkContext.MqttReady, AppLinkContext.LedState);
            continue;
        }

        if (xSemaphoreTake(AppUsart2RxSemaphore, pdMS_TO_TICKS(50U)) == pdTRUE)
        {
            xSemaphoreTake(AppESPCommandMutex, portMAX_DELAY);
            AppTasks_LinkStep(&AppLinkContext);
            xSemaphoreGive(AppESPCommandMutex);
            AppRTOS_SetSharedState(AppLinkContext.MqttReady, AppLinkContext.LedState);
        }
    }
}

/* 保持采样节奏稳定，并仅发布最新的样本。 */
static void AppFreeRTOS_SensorTask(void *Argument)
{
    TickType_t LastWakeTime;
    AppTasks_SensorSample Sample;

    (void)Argument;
    while (AppLinkBootReady == 0U)
    {
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
    LastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        if (AppRTOS_ReadSensorSample(&Sample) != 0U)
        {
            /* 遥测仅关心最新的样本，因此故意丢弃过时的样本。 */
            (void)xQueueOverwrite(AppSensorQueue, &Sample);
        }
        vTaskDelayUntil(&LastWakeTime, pdMS_TO_TICKS(APP_RTOS_SENSOR_PERIOD_MS));
    }
}

/* 将传感器采集与发布延迟解耦，并仅在 MQTT 就绪时刷新 OLED。 */
static void AppFreeRTOS_TelemetryTask(void *Argument)
{
    AppTasks_SensorSample Sample;
    uint8_t MqttReady;
    uint8_t LedState;

    (void)Argument;
    memset(&Sample, 0, sizeof(Sample));

    for (;;)
    {
        if (xQueueReceive(AppSensorQueue, &Sample, portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        AppRTOS_GetSharedState(&MqttReady, &LedState);
        if ((MqttReady != 0U) && (Sample.Valid != 0U))
        {
            (void)xQueueSend(AppPublishQueue, &Sample, 0U);
            AppRTOS_ShowSample(&Sample, LedState);
        }
    }
}

/* 创建队列、互斥锁和任务；main() 仍负责启动调度器。 */
uint8_t AppFreeRTOS_Start(void)
{
    AppSensorQueue = xQueueCreate(APP_RTOS_SENSOR_QUEUE_LENGTH, sizeof(AppTasks_SensorSample));
    if (AppSensorQueue == NULL)
    {
        return 0U;
    }

    AppPublishQueue = xQueueCreate(APP_RTOS_PUBLISH_QUEUE_LENGTH, sizeof(AppTasks_SensorSample));
    if (AppPublishQueue == NULL)
    {
        return 0U;
    }

#if (APP_USART1_ENABLE != 0U)
    AppWiFiCommandQueue = xQueueCreate(APP_RTOS_WIFI_QUEUE_LENGTH, sizeof(AppTasks_WiFiCommand));
    if (AppWiFiCommandQueue == NULL)
    {
        return 0U;
    }
#endif

    AppUsart1RxSemaphore = xSemaphoreCreateBinary();
    AppUsart2RxSemaphore = xSemaphoreCreateBinary();

    AppLogMutex = xSemaphoreCreateMutex();
    AppOLEDMutex = xSemaphoreCreateMutex();
    AppESPCommandMutex = xSemaphoreCreateMutex();
    AppStateMutex = xSemaphoreCreateMutex();
    if ((AppUsart2RxSemaphore == NULL) ||
        (AppLogMutex == NULL) ||
        (AppOLEDMutex == NULL) ||
        (AppESPCommandMutex == NULL) ||
        (AppStateMutex == NULL))
    {
        return 0U;
    }

#if (APP_USART1_ENABLE != 0U)
    if (AppUsart1RxSemaphore == NULL)
    {
        return 0U;
    }
#endif

    AppRTOS_SetSharedState(0U, 0U);
    if (xTaskCreate(AppFreeRTOS_LinkTask,
                    "link",
                    APP_RTOS_LINK_TASK_STACK_WORDS,
                    0,
                    APP_RTOS_LINK_TASK_PRIORITY,
                    0) != pdPASS)
    {
        return 0U;
    }

    if (xTaskCreate(AppFreeRTOS_SensorTask,
                    "sensor",
                    APP_RTOS_SENSOR_TASK_STACK_WORDS,
                    0,
                    APP_RTOS_SENSOR_TASK_PRIORITY,
                    0) != pdPASS)
    {
        return 0U;
    }

    if (xTaskCreate(AppFreeRTOS_TelemetryTask,
                    "tele",
                    APP_RTOS_TELEMETRY_TASK_STACK_WORDS,
                    0,
                    APP_RTOS_TELEMETRY_TASK_PRIORITY,
                    0) != pdPASS)
    {
        return 0U;
    }

#if (APP_USART1_ENABLE != 0U)
    if (xTaskCreate(AppFreeRTOS_CommandTask,
                    "cmd",
                    APP_RTOS_COMMAND_TASK_STACK_WORDS,
                    0,
                    APP_RTOS_COMMAND_TASK_PRIORITY,
                    0) != pdPASS)
    {
        return 0U;
    }
#endif

    return 1U;
}

/* 当 FreeRTOS 堆分配失败时，在可诊断状态下停止系统。 */
void vApplicationMallocFailedHook(void)
{
#if (APP_USART1_ENABLE != 0U)
    AppRTOS_LogPrintf("[RTOS] malloc failed\r\n");
#endif
    AppFreeRTOS_ShowFatal("MALLOC", "CHECK HEAP");
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

/* 当任务耗尽其堆栈预算时，在可诊断状态下停止系统。 */
void vApplicationStackOverflowHook(TaskHandle_t Task, char *TaskName)
{
    (void)Task;
#if (APP_USART1_ENABLE != 0U)
    AppRTOS_LogPrintf("[RTOS] stack overflow: %s\r\n", (TaskName != 0) ? TaskName : "unknown");
#else
    (void)TaskName;
#endif
    AppFreeRTOS_ShowFatal("STACK", "CHECK TASK");
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
