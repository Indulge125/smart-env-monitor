# 智境：基于 FreeRTOS 的室内环境智能感知与调控系统

基于 **STM32F103C8T6** 与 **FreeRTOS** 的室内环境监测及设备控制项目。系统采集环境光照和温度数据，在 OLED 上显示状态；支持按键交互、舵机 PWM 控制、LED/蜂鸣器告警，并通过 UART 与 DTU/边缘网关进行 JSON 数据交互。

## 主要功能

- 使用 ADC 采集光照、温度等模拟量，并通过滑动平均滤波平滑数据。
- 使用 FreeRTOS 将传感器采集、显示、控制、通信和按键处理拆分为独立任务。
- 通过 OLED 显示环境数据与运行状态，支持按键切换自动、手动和设置模式。
- 依据阈值控制舵机、LED 与蜂鸣器，实现环境告警和执行器联动。
- 通过 UART 输出遥测 JSON，并解析来自网关的控制指令。

## 软件与硬件

- MCU：STM32F103C8T6（Cortex-M3）
- 外设：ADC、DMA、GPIO、TIM PWM、USART、OLED、按键、LED、蜂鸣器、舵机
- RTOS：FreeRTOS（CMSIS-RTOS v2 接口）
- 开发工具：STM32CubeMX、Keil MDK-ARM

## 目录说明

```text
smart_env_monitor/
├── Core/           # STM32CubeMX 生成的初始化代码和 FreeRTOS 任务逻辑
├── Drivers/        # STM32 HAL 与 CMSIS 驱动
├── Middlewares/    # FreeRTOS 源码
├── User/           # OLED、延时等自定义驱动
├── MDK-ARM/        # Keil uVision 工程文件
└── smart_env_monitor.ioc  # STM32CubeMX 配置
```

## 构建与烧录

1. 使用 Keil MDK-ARM 打开 `MDK-ARM/smart_env_monitor.uvprojx`。
2. 确认已安装 STM32F1 设备包，并连接 ST-Link。
3. 编译工程后下载至开发板。

> 编译生成的目标文件、日志和本机 IDE 配置已通过 `.gitignore` 排除，不纳入版本控制。
