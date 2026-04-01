# 软件架构说明

## 1. 总体目标

这个项目实现了一个基于 `STM32F103C8 + ESP8266` 的轻量级物联网边缘节点：

- 采集温湿度与光照数据
- 通过 MQTT 周期上报遥测
- 订阅控制主题并远程控制 LED
- 用 OLED 提供本地状态可视化

代码同时保留了两种运行形态：

- 裸机 `super-loop`
- `FreeRTOS` 任务调度

两种模式通过 [`User/app_config.h`](../User/app_config.h) 中的编译期开关切换，方便用于课程实验、架构对比和面试讲解。

## 2. 分层设计

### 硬件抽象层

- [`Hardware/serial.c`](../Hardware/serial.c)：`USART1/USART2` 驱动，中断接收 + FIFO 缓冲
- [`Hardware/OLED.c`](../Hardware/OLED.c)：OLED 显示
- [`Hardware/LED.c`](../Hardware/LED.c)：LED 控制
- [`Hardware/Sth30.c`](../Hardware/Sth30.c)：温湿度传感器驱动
- [`Hardware/BH1750.c`](../Hardware/BH1750.c)：光照传感器驱动

### 设备通信层

- [`Hardware/esp8266.c`](../Hardware/esp8266.c)

负责封装 ESP8266 的常用 AT 指令，包括：

- 串口初始化
- 模组在线检测
- 波特率自恢复
- Wi-Fi 建链
- MQTT 连接、订阅与发布

### 协议与业务层

- [`Hardware/MQTT.C`](../Hardware/MQTT.C)

负责：

- `WIFI:ssid,password` 命令解析
- Wi-Fi + MQTT 会话建立
- 遥测 JSON 拼装
- `+MQTTSUBRECV` 异步下行消息解析
- LED 控制命令处理

### 应用调度层

- [`User/main.c`](../User/main.c)
- [`User/app_tasks.c`](../User/app_tasks.c)
- [`User/app_freertos.c`](../User/app_freertos.c)

负责把“命令处理、链路维护、传感器采样、周期上报、显示刷新”组织成完整运行流程。

## 3. 关键数据流

### 遥测上行

1. `Sth30` / `BH1750` 读取传感器数据
2. 业务层拼装遥测 JSON
3. `ESP8266_MQTTPublish()` 通过 MQTT 发布
4. OLED 同步显示最新遥测摘要

### 控制下行

1. Broker 向控制主题下发 payload
2. ESP8266 输出 `+MQTTSUBRECV`
3. `MQTTX_ConsumeAsyncByte()` 按字节驱动状态机解析消息
4. 业务层识别 `led=0/1`
5. 更新 LED 状态并回发最新状态消息

### 手动联网

1. 用户通过 `USART1` 输入 `WIFI:ssid,password`
2. 应用层缓存并提交命令
3. 业务层解析 SSID/密码
4. 执行 Wi-Fi + MQTT 建链流程

## 4. 裸机模式

裸机模式下，主循环位于 [`User/main.c`](../User/main.c)：

1. `AppTasks_CommandStep`
2. `AppTasks_LinkStep`
3. `AppTasks_TelemetryStep`
4. `AppTasks_DisplayStep`
5. `Delay_ms(1)` + `AppTasks_Tick1ms`

特点：

- 结构直观，适合快速演示
- 资源占用更低
- 对阻塞调用更敏感，可扩展性一般

## 5. FreeRTOS 模式

FreeRTOS 模式下，业务被拆成多个明确任务：

- `LinkTask`：唯一直接访问 ESP8266 的链路任务
- `SensorTask`：定期采样
- `TelemetryTask`：转发待发布数据并刷新 OLED
- `CommandTask`：接收串口命令并投递到队列

配套的 RTOS 机制：

- `Queue`：采样数据、发布请求、Wi-Fi 命令
- `Binary Semaphore`：USART 接收事件唤醒
- `Mutex`：日志、OLED/I2C、ESP8266 命令串行化、共享状态保护

这样的拆分让面试时更容易说明：

- 为什么要把“输入解析”和“ESP 操作”解耦
- 为什么“真正发 MQTT”只放在一个任务中
- 为什么 OLED/I2C 访问需要互斥

## 6. 设计取舍

### 当前方案的优点

- 适合 STM32F103 这类资源有限 MCU
- 结构分层清晰，方便讲解
- OLED 和串口日志让调试路径比较完整
- 既能体现底层驱动，也能体现 RTOS 任务设计

### 当前方案的限制

- 依赖 ESP-AT 的 MQTT 指令集，灵活性受模组固件限制
- 传感器/OLED 共用 I2C，访问需要严格串行化
- 主题、Broker、Client ID 主要通过宏配置，动态配置能力有限
- `LED2_*` 仍是预留接口

## 7. 适合在面试中强调的点

- 为什么要同时保留裸机和 RTOS 两种实现路径
- 串口中断 + FIFO + 状态机解析的组合设计
- 如何避免多个任务同时访问 ESP8266 和 OLED
- 为什么公开仓库默认关闭 Wi-Fi 自动连接
