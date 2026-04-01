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

/* app_freertos.c
 * 这个文件负责把应用层业务拆分到 FreeRTOS 环境中运行，主要包含：
 * 1. RTOS 资源创建，例如队列、信号量、互斥量；
 * 2. 任务之间的协作逻辑，例如采样、显示、链路维护、命令处理；
 * 3. 中断与任务之间的衔接，例如串口中断通过信号量唤醒任务；
 * 4. RTOS 异常钩子，例如内存申请失败和栈溢出时的停机处理。
 */

#define APP_RTOS_LINK_TASK_STACK_WORDS       512U /* 链路任务逻辑最重，要处理 ESP8266、AT 命令、MQTT 等流程，栈给得更大。 */
#define APP_RTOS_LINK_TASK_PRIORITY          3U   /* 链路任务优先级最高，确保网络状态机和收发处理更及时。 */
#define APP_RTOS_TELEMETRY_TASK_STACK_WORDS  512U /* 遥测任务会做显示和发布转发，局部变量相对较多，栈适当放大。 */
#define APP_RTOS_TELEMETRY_TASK_PRIORITY     2U   /* 遥测任务属于常规业务优先级，低于链路，高于空闲任务。 */
#define APP_RTOS_SENSOR_TASK_STACK_WORDS     256U /* 采样任务逻辑较简单，以周期读取为主，较小栈即可。 */
#define APP_RTOS_SENSOR_TASK_PRIORITY        2U   /* 采样任务与遥测同级，靠时间片轮转共享 CPU。 */
#define APP_RTOS_COMMAND_TASK_STACK_WORDS    256U /* 命令任务只做串口组包和投递，栈需求较小。 */
#define APP_RTOS_COMMAND_TASK_PRIORITY       2U   /* 命令任务与采样/遥测同级，避免长期压制其它业务任务。 */
#define APP_RTOS_SENSOR_QUEUE_LENGTH         1U   /* 传感器队列只保留“最新一帧”数据，因此长度设为 1 并配合覆盖写入。 */
#define APP_RTOS_PUBLISH_QUEUE_LENGTH        2U   /* 发布队列做轻量缓冲，允许采样与链路发布之间短暂解耦。 */
#define APP_RTOS_WIFI_QUEUE_LENGTH           4U   /* 串口命令队列允许积压少量待执行命令，避免输入稍快就丢包。 */
#define APP_RTOS_SENSOR_PERIOD_MS            MQTTX_TELEMETRY_INTERVAL_MS /* 采样周期与遥测发送周期保持一致，减少无效采样。 */

typedef struct
{
    uint8_t MqttReady; /* 当前 MQTT 链路是否可用，供其它任务快速判断是否允许发布。 */
    uint8_t LedState;  /* 当前远端或链路逻辑维护的 LED 状态，用于显示和上报。 */
} AppRTOS_SharedState;

static AppTasks_Context AppLinkContext;              /* 链路上下文，集中保存 WiFi/MQTT 运行状态。 */
static AppRTOS_SharedState AppSharedState;           /* 多任务共享的轻量状态，通过互斥量保护。 */
static QueueHandle_t AppSensorQueue = NULL;          /* 采样任务 -> 遥测任务：传递最新采样值。 */
static QueueHandle_t AppPublishQueue = NULL;         /* 遥测任务 -> 链路任务：传递待发布消息。 */
static QueueHandle_t AppWiFiCommandQueue = NULL;     /* 命令任务 -> 链路任务：传递待执行 WiFi 命令。 */
static SemaphoreHandle_t AppUsart1RxSemaphore = NULL; /* USART1 接收事件信号量。 */
static SemaphoreHandle_t AppUsart2RxSemaphore = NULL; /* USART2 接收事件信号量。 */
static SemaphoreHandle_t AppLogMutex = NULL;         /* 串口日志互斥量，防止输出串行化被打乱。 */
static SemaphoreHandle_t AppOLEDMutex = NULL;        /* OLED/I2C 访问互斥量。 */
static SemaphoreHandle_t AppESPCommandMutex = NULL;  /* ESP8266 命令互斥量。 */
static SemaphoreHandle_t AppStateMutex = NULL;       /* 共享状态互斥量。 */
static volatile uint8_t AppLinkBootReady = 0U;       /* 链路任务启动完成标记，供采样任务延后启动。 */

/* 更新共享状态。
 * 这里用互斥量保护，是因为链路任务会写状态，遥测任务等会读状态。
 */
static void AppRTOS_SetSharedState(uint8_t MqttReady, uint8_t LedState)
{
    if (AppStateMutex != NULL)
    {
        /* 调度器启动后在任务上下文中调用，允许一直等待拿到锁。 */
        xSemaphoreTake(AppStateMutex, portMAX_DELAY);
    }

    /* 共享状态很小，直接整体按字段覆盖。 */
    AppSharedState.MqttReady = MqttReady;
    AppSharedState.LedState = LedState;

    if (AppStateMutex != NULL)
    {
        xSemaphoreGive(AppStateMutex);
    }
}

/* 读取共享状态。
 * 调用者可以只关心某一个字段，因此允许传入空指针跳过不需要的输出。
 */
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

/* 锁住 OLED 以及与其共享 I2C 的访问窗口。 */
void AppRTOS_OLEDLock(void)
{
    if (AppOLEDMutex != NULL)
    {
        xSemaphoreTake(AppOLEDMutex, portMAX_DELAY);
    }
}

/* 释放 OLED 访问锁。 */
void AppRTOS_OLEDUnlock(void)
{
    if (AppOLEDMutex != NULL)
    {
        xSemaphoreGive(AppOLEDMutex);
    }
}

/* 清屏的简单封装，保证外部调用时自动带互斥保护。 */
void AppRTOS_OLEDClear(void)
{
    AppRTOS_OLEDLock();
    OLED_Clear();
    AppRTOS_OLEDUnlock();
}

/* 在指定行显示一段文本。
 * 先清空该行再写入，避免较短字符串残留旧字符。
 */
void AppRTOS_OLEDShowLine(uint8_t Line, const char *Text)
{
    AppRTOS_OLEDLock();
    OLED_ShowString(Line, 1, "                ");
    OLED_ShowString(Line, 1, (char *)Text);
    AppRTOS_OLEDUnlock();
}

/* 向其它模块暴露 ESP 命令互斥量，便于跨文件统一串行化 ESP8266 访问。 */
SemaphoreHandle_t AppRTOS_GetESPCommandMutex(void)
{
    return AppESPCommandMutex;
}

/* 串口接收中断里的任务唤醒入口。
 * 中断里不做重逻辑，只负责通过信号量告诉对应任务“有新数据到了”。
 */
BaseType_t AppRTOS_GiveRxSemaphoreFromISR(USART_TypeDef *USARTx)
{
    BaseType_t HigherPriorityTaskWoken = pdFALSE;

    if ((USARTx == USART1) && (AppUsart1RxSemaphore != NULL))
    {
        /* USART1 用于人机命令/日志串口，唤醒命令任务。 */
        xSemaphoreGiveFromISR(AppUsart1RxSemaphore, &HigherPriorityTaskWoken);
    }
    else if ((USARTx == USART2) && (AppUsart2RxSemaphore != NULL))
    {
        /* USART2 用于 ESP8266 通讯，唤醒链路任务处理回包。 */
        xSemaphoreGiveFromISR(AppUsart2RxSemaphore, &HigherPriorityTaskWoken);
    }

    /* 调用方可根据返回值决定是否需要在 ISR 退出前触发一次任务切换。 */
    return HigherPriorityTaskWoken;
}

/* 带互斥保护的日志输出。
 * 先格式化到本地缓冲区，再一次性发串口，降低多任务输出互相穿插的概率。
 */
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

    /* 多个任务可能同时打日志，因此这里必须串行化。 */
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

/* 在 OLED 上显示致命错误信息。
 * 发生不可恢复错误时，不再尝试保留原界面，直接全屏改成故障提示。
 */
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

/* 读取一帧传感器数据。
 * 成功时填充 Sample 并置 Valid 标志；失败时在 OLED 上提示传感器/I2C 故障。
 */
static uint8_t AppRTOS_ReadSensorSample(AppTasks_SensorSample *Sample)
{
    if (Sample == 0)
    {
        return 0U;
    }

    memset(Sample, 0, sizeof(*Sample));
    /* OLED、STH30、BH1750 共用 I2C1，因此采样期间要和显示刷新互斥。 */
    AppRTOS_OLEDLock();
    if ((STH30_ReadData(&Sample->Climate) != 0U) || (BH1750_ReadLux(&Sample->Lux) != 0U))
    {
        AppRTOS_OLEDUnlock();
        /* 采样失败时直接给出面向硬件排查的提示。 */
        AppRTOS_OLEDShowLine(1, "MQTT ONLINE");
        AppRTOS_OLEDShowLine(2, "SENSOR FAIL");
        AppRTOS_OLEDShowLine(3, "CHECK I2C");
        AppRTOS_OLEDShowLine(4, "PB6 PB7");
        return 0U;
    }
    AppRTOS_OLEDUnlock();

    /* 只有全部外设读取成功时才把样本标成有效。 */
    Sample->Valid = 1U;
    return 1U;
}

/* 把最新样本格式化后显示到 OLED 上。 */
static void AppRTOS_ShowSample(const AppTasks_SensorSample *Sample, uint8_t LedState)
{
    char Line[17];
    int16_t Temperature;
    uint16_t TemperatureFraction;

    if ((Sample == 0) || (Sample->Valid == 0U))
    {
        return;
    }

    /* 传感器温度按 0.1 摄氏度存储，这里拆成整数和小数部分便于显示。 */
    Temperature = Sample->Climate.Temperature;
    TemperatureFraction = (uint16_t)((Temperature >= 0) ? (Temperature % 10) : (-Temperature % 10));

    AppRTOS_OLEDLock();
    /* 第 1 行显示联网状态。 */
    OLED_ShowString(1, 1, "                ");
    OLED_ShowString(1, 1, "MQTT ONLINE");
    /* 第 2 行显示温湿度。 */
    sprintf(Line, "T:%d.%uC H:%u%%", Temperature / 10, TemperatureFraction, Sample->Climate.Humidity / 10);
    OLED_ShowString(2, 1, "                ");
    OLED_ShowString(2, 1, Line);
    /* 第 3 行显示光照和 LED 状态。 */
    sprintf(Line, "L:%ulx LED:%s", (unsigned int)Sample->Lux, (LedState != 0U) ? "ON" : "OFF");
    OLED_ShowString(3, 1, "                ");
    OLED_ShowString(3, 1, Line);
    /* 第 4 行提示串口命令功能可用。 */
    OLED_ShowString(4, 1, "                ");
    OLED_ShowString(4, 1, "CMD READY");
    AppRTOS_OLEDUnlock();
}

/* 把一帧采样数据编码成 JSON 并发布到遥测主题。 */
static uint8_t AppRTOS_PublishSample(const AppTasks_SensorSample *Sample, uint8_t LedState, char *Response, uint16_t ResponseSize)
{
    char Payload[160];
    int16_t Temperature;
    uint16_t TemperatureFraction;

    if ((Sample == 0) || (Sample->Valid == 0U))
    {
        return 0U;
    }

    /* 发布前先把内部结构格式化为云端更容易消费的 JSON 文本。 */
    Temperature = Sample->Climate.Temperature;
    TemperatureFraction = (uint16_t)((Temperature >= 0) ? (Temperature % 10) : (-Temperature % 10));
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
/* 把当前组装完成的一条串口命令送入待执行队列。 */
static uint8_t AppRTOS_SubmitCommand(char *CommandBuffer, uint16_t *CommandLength)
{
    AppTasks_WiFiCommand Command;

    /* 空命令直接丢弃，避免回车键触发无意义任务。 */
    if (*CommandLength == 0U)
    {
        return 0U;
    }

    /* 拷贝到固定长度结构体中，并主动补上字符串结束符。 */
    memcpy(Command.Command, CommandBuffer, *CommandLength);
    Command.Command[*CommandLength] = '\0';
    *CommandLength = 0U;

    /* 使用阻塞发送，确保命令不会因为瞬时队列满而悄悄丢失。 */
    if (xQueueSend(AppWiFiCommandQueue, &Command, portMAX_DELAY) != pdPASS)
    {
        return 0U;
    }

    return 1U;
}

/* 串口命令任务：
 * 1. 等待 USART1 接收事件信号量；
 * 2. 从串口 FIFO 中持续取字节并组装成一行命令；
 * 3. 碰到回车换行或空闲超时后，把命令投递到 AppWiFiCommandQueue；
 * 4. 真正执行命令的不是本任务，而是链路任务，目的是把“输入解析”和“ESP 操作”解耦。
 */
static void AppFreeRTOS_CommandTask(void *Argument)
{
    char CommandBuffer[APP_TASKS_WIFI_COMMAND_SIZE];
    uint16_t CommandLength = 0U;
    TickType_t LastRxTick = xTaskGetTickCount();

    (void)Argument;
    memset(CommandBuffer, 0, sizeof(CommandBuffer));

    for (;;)
    {
        /* 等待串口接收中断发来的“有数据”通知。 */
        if (xSemaphoreTake(AppUsart1RxSemaphore, pdMS_TO_TICKS(APP_TASKS_WIFI_COMMAND_IDLE_MS)) == pdTRUE)
        {
            uint8_t PcData;

            /* 把当前 FIFO 里已经收到的字节尽可能一次性取空。 */
            while (Serial_ReadByteNonBlocking(USART1, &PcData) != 0U)
            {
                LastRxTick = xTaskGetTickCount();
                if ((PcData == '\r') || (PcData == '\n'))
                {
                    /* 回车换行视为命令结束。 */
                    (void)AppRTOS_SubmitCommand(CommandBuffer, &CommandLength);
                }
                else if ((PcData == 0x08U) || (PcData == 0x7FU))
                {
                    /* 支持退格键修正输入。 */
                    if (CommandLength != 0U)
                    {
                        CommandLength--;
                    }
                }
                else if (CommandLength < (APP_TASKS_WIFI_COMMAND_SIZE - 1U))
                {
                    /* 只有缓冲区还有空间时才继续收字符，始终给 '\0' 预留位置。 */
                    CommandBuffer[CommandLength++] = (char)PcData;
                }
            }
        }
        else if ((CommandLength != 0U) &&
                 ((xTaskGetTickCount() - LastRxTick) >= pdMS_TO_TICKS(APP_TASKS_WIFI_COMMAND_IDLE_MS)))
        {
            /* 输入停顿太久也自动提交，避免用户没敲回车时命令一直悬着。 */
            AppRTOS_LogPrintf("[CMD] auto-submit on idle\r\n");
            (void)AppRTOS_SubmitCommand(CommandBuffer, &CommandLength);
        }
    }
}
#endif

/* 链路任务：
 * 1. 是系统里与 ESP8266/MQTT 直接打交道的核心任务；
 * 2. 启动阶段可执行自动联网和自动连云；
 * 3. 运行阶段轮询处理三类事件：外部命令、待发布数据、USART2 回包事件；
 * 4. 通过 AppESPCommandMutex 串行化 ESP 访问，避免多个任务同时操作串口模组。
 */
static void AppFreeRTOS_LinkTask(void *Argument)
{
    char Response[APP_TASKS_RESPONSE_SIZE];

    (void)Argument;
    /* 进入任务后先把链路上下文初始化到已知状态。 */
    AppTasks_InitContext(&AppLinkContext);
    memset(Response, 0, sizeof(Response));
    AppRTOS_SetSharedState(0U, 0U);
    /* 链路初始化前，采样任务先不要启动。 */
    AppLinkBootReady = 0U;

#if (APP_WIFI_AUTO_CONNECT_ENABLE != 0U)
    /* 给 ESP8266 上电后留一点稳定时间，再开始自动联网流程。 */
    AppRTOS_LogPrintf("[RTOS] wait ESP settle %u ms\r\n", (unsigned int)APP_WIFI_AUTO_START_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(APP_WIFI_AUTO_START_DELAY_MS));
    xSemaphoreTake(AppESPCommandMutex, portMAX_DELAY);
    AppTasks_RunBootAutoConnect(&AppLinkContext, APP_WIFI_AUTO_SSID, APP_WIFI_AUTO_PASSWORD);
    xSemaphoreGive(AppESPCommandMutex);
    AppRTOS_SetSharedState(AppLinkContext.MqttReady, AppLinkContext.LedState);
#endif
    /* 链路启动阶段结束，允许采样任务进入工作状态。 */
    AppLinkBootReady = 1U;

    for (;;)
    {
        AppTasks_SensorSample PublishSample;

#if (APP_USART1_ENABLE != 0U)
        AppTasks_WiFiCommand Command;

        /* 优先处理人工下发的控制命令，提升交互响应性。 */
        if (xQueueReceive(AppWiFiCommandQueue, &Command, 0U) == pdPASS)
        {
            xSemaphoreTake(AppESPCommandMutex, portMAX_DELAY);
            /* 执行新命令前重置 MQTT 命令上下文，避免沿用脏状态。 */
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

        /* 再处理待发布遥测数据，真正的网络发送只在链路任务中执行。 */
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

        /* 最后阻塞等待 ESP8266 的串口回包，驱动链路状态机前进。 */
        if (xSemaphoreTake(AppUsart2RxSemaphore, pdMS_TO_TICKS(50U)) == pdTRUE)
        {
            xSemaphoreTake(AppESPCommandMutex, portMAX_DELAY);
            AppTasks_LinkStep(&AppLinkContext);
            xSemaphoreGive(AppESPCommandMutex);
            AppRTOS_SetSharedState(AppLinkContext.MqttReady, AppLinkContext.LedState);
        }
    }
}

/* 采样任务：
 * 1. 等待链路启动阶段完成，避免系统还没稳定就开始业务采集；
 * 2. 使用 vTaskDelayUntil() 做严格周期调度，减小采样周期漂移；
 * 3. 每次只保留最新采样值，因此使用 xQueueOverwrite() 覆盖旧数据。
 */
static void AppFreeRTOS_SensorTask(void *Argument)
{
    TickType_t LastWakeTime;
    AppTasks_SensorSample Sample;

    (void)Argument;
    /* 等待链路任务至少完成基础初始化，避免系统启动阶段资源竞争过早发生。 */
    while (AppLinkBootReady == 0U)
    {
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
    /* 记录周期任务的初始参考时刻。 */
    LastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        /* 采样成功后覆盖写入“最新值”队列。 */
        if (AppRTOS_ReadSensorSample(&Sample) != 0U)
        {
            (void)xQueueOverwrite(AppSensorQueue, &Sample);
        }
        /* 固定周期唤醒，比简单 vTaskDelay 更适合定频采样。 */
        vTaskDelayUntil(&LastWakeTime, pdMS_TO_TICKS(APP_RTOS_SENSOR_PERIOD_MS));
    }
}

/* 遥测任务：
 * 1. 阻塞等待新的传感器数据；
 * 2. 读取共享状态判断 MQTT 是否已经可用；
 * 3. 若链路就绪且采样有效，则把数据送入发布队列，并同步刷新 OLED；
 * 4. 这样可以把“采样”和“联网发布”拆成两个阶段，减少互相阻塞。
 */
static void AppFreeRTOS_TelemetryTask(void *Argument)
{
    AppTasks_SensorSample Sample;
    uint8_t MqttReady;
    uint8_t LedState;

    (void)Argument;
    memset(&Sample, 0, sizeof(Sample));

    for (;;)
    {
        /* 没有新样本时一直阻塞等待，不空转占 CPU。 */
        if (xQueueReceive(AppSensorQueue, &Sample, portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        /* 发布前重新读取链路状态，避免用过期状态决策。 */
        AppRTOS_GetSharedState(&MqttReady, &LedState);
        if ((MqttReady != 0U) && (Sample.Valid != 0U))
        {
            /* 只负责把数据送进发布链路，不在本任务里直接访问 ESP8266。 */
            (void)xQueueSend(AppPublishQueue, &Sample, 0U);
            AppRTOS_ShowSample(&Sample, LedState);
        }
    }
}

/* RTOS 启动入口：
 * 1. 先创建任务间通信对象，包括队列、二值信号量和互斥量；
 * 2. 再创建各个业务任务，但此时任务还不会真正运行；
 * 3. 只有 main() 里调用 vTaskStartScheduler() 后，调度器才会开始切换到这些任务；
 * 4. 任一步骤失败都返回 0，交由上层统一报错并停机。
 */
uint8_t AppFreeRTOS_Start(void)
{
    /* 采样队列：
     * 存放最新传感器样本，长度为 1，后续由采样任务使用覆盖写入，
     * 遥测任务从这里取走当前最新值。
     */
    AppSensorQueue = xQueueCreate(APP_RTOS_SENSOR_QUEUE_LENGTH, sizeof(AppTasks_SensorSample));
    if (AppSensorQueue == NULL)
    {
        return 0U;
    }

    /* 发布队列：
     * 遥测任务把“准备发布到 MQTT 的样本”放进来，
     * 链路任务从该队列取出并执行真正的网络发送。
     */
    AppPublishQueue = xQueueCreate(APP_RTOS_PUBLISH_QUEUE_LENGTH, sizeof(AppTasks_SensorSample));
    if (AppPublishQueue == NULL)
    {
        return 0U;
    }

#if (APP_USART1_ENABLE != 0U)
    /* 命令队列：
     * 串口命令任务把用户输入整理成完整命令后送入这里，
     * 再由链路任务统一执行，避免多个上下文直接操作 ESP8266。
     */
    AppWiFiCommandQueue = xQueueCreate(APP_RTOS_WIFI_QUEUE_LENGTH, sizeof(AppTasks_WiFiCommand));
    if (AppWiFiCommandQueue == NULL)
    {
        return 0U;
    }
#endif

    /* 二值信号量：
     * AppUsart1RxSemaphore 用于通知“调试串口/命令串口有新数据”；
     * AppUsart2RxSemaphore 用于通知“ESP8266 串口有新回包”。
     */
    AppUsart1RxSemaphore = xSemaphoreCreateBinary();
    AppUsart2RxSemaphore = xSemaphoreCreateBinary();

    /* 互斥量：
     * AppLogMutex       保护日志打印，避免多任务输出交叉；
     * AppOLEDMutex      保护 OLED 显示接口，避免多任务同时刷屏；
     * AppESPCommandMutex 保护 ESP8266 访问，确保 AT/MQTT 交互串行执行；
     * AppStateMutex     保护共享状态结构 AppSharedState。
     */
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
    /* 串口命令功能启用时，USART1 对应的接收信号量也必须创建成功。 */
    if (AppUsart1RxSemaphore == NULL)
    {
        return 0U;
    }
#endif

    /* 启动时默认认为 MQTT 未就绪、LED 关闭，避免其它任务读到随机初值。 */
    AppRTOS_SetSharedState(0U, 0U);

    /* xTaskCreate 参数顺序说明：
     * 1. 任务入口函数；
     * 2. 任务名，仅用于调试和查看；
     * 3. 栈深度，单位通常是 StackType_t 个数，不是字节；
     * 4. 传给任务的参数，这里都不需要，统一传 0；
     * 5. 任务优先级；
     * 6. 任务句柄输出，这里没有保存句柄，因此传 0。
     */

    /* 创建链路任务：
     * 该任务是网络侧“总调度者”，负责开机联网、处理 ESP8266 回包、
     * 执行串口输入命令，以及把待发布数据真正发往 MQTT。
     */
    if (xTaskCreate(AppFreeRTOS_LinkTask,
                    "link",
                    APP_RTOS_LINK_TASK_STACK_WORDS,
                    0,
                    APP_RTOS_LINK_TASK_PRIORITY,
                    0) != pdPASS)
    {
        return 0U;
    }

    /* 创建采样任务：
     * 该任务只负责按固定周期采样，不直接做网络发送，
     * 这样可以把时间敏感的采样逻辑和可能阻塞的网络逻辑隔离开。
     */
    if (xTaskCreate(AppFreeRTOS_SensorTask,
                    "sensor",
                    APP_RTOS_SENSOR_TASK_STACK_WORDS,
                    0,
                    APP_RTOS_SENSOR_TASK_PRIORITY,
                    0) != pdPASS)
    {
        return 0U;
    }

    /* 创建遥测任务：
     * 它处在“采样”和“联网”之间，负责检查样本有效性与链路状态，
     * 满足条件时把数据送到发布队列，并更新本地显示。
     */
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
    /* 创建命令任务：
     * 它只负责接收和组装用户从 USART1 输入的命令，
     * 命令不会在本任务里直接执行，而是投递给链路任务统一处理。
     */
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

    /* 所有资源和任务都创建成功，返回给 main() 继续启动调度器。 */
    return 1U;
}

/* 动态内存申请失败钩子。
 * 在当前工程里通常意味着 FreeRTOS 堆太小，或者某些对象创建时栈/队列配置过大。
 */
void vApplicationMallocFailedHook(void)
{
#if (APP_USART1_ENABLE != 0U)
    AppRTOS_LogPrintf("[RTOS] malloc failed\r\n");
#endif
    AppFreeRTOS_ShowFatal("MALLOC", "CHECK HEAP");
    /* 进入不可恢复错误路径后关闭中断并停机。 */
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

/* 任务栈溢出钩子。
 * 一旦进入这里，说明至少有一个任务栈深度配置不足或递归/局部变量使用异常。
 */
void vApplicationStackOverflowHook(TaskHandle_t Task, char *TaskName)
{
    (void)Task;
#if (APP_USART1_ENABLE != 0U)
    AppRTOS_LogPrintf("[RTOS] stack overflow: %s\r\n", (TaskName != 0) ? TaskName : "unknown");
#else
    (void)TaskName;
#endif
    AppFreeRTOS_ShowFatal("STACK", "CHECK TASK");
    /* 栈溢出后系统状态已不可信，直接停机等待排查。 */
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
