# 智境：基于 FreeRTOS 的室内环境智能感知与调控系统

基于 **STM32F103C8T6** 与 **FreeRTOS** 的室内环境监测及设备控制项目。系统采集环境光照和温度数据，在 OLED 上显示状态；支持按键交互、舵机 PWM 控制、LED/蜂鸣器告警，并通过 UART 与 DTU/边缘网关进行 JSON 数据交互。

## 主要功能

- 使用 ADC 采集光照、温度等模拟量，并通过 16 点滑动平均滤波平滑数据。
- 使用 FreeRTOS 将传感器采集、显示、控制、通信和按键处理拆分为独立任务。
- 通过 OLED 显示环境数据与运行状态，支持按键切换自动、手动和设置模式。
- 依据阈值控制舵机、LED 与蜂鸣器，实现环境告警和执行器联动。
- 通过 UART 输出遥测 JSON，并解析来自网关的控制指令。

## 软件与硬件

- MCU：STM32F103C8T6（Cortex-M3，72 MHz）
- 外设：ADC、TIM PWM、USART×2、OLED、按键、LED、蜂鸣器、舵机
- RTOS：FreeRTOS（CMSIS-RTOS v2 接口）
- 开发工具：STM32CubeMX、Keil MDK-ARM（Arm Compiler AC5）

## 引脚接线表

| 功能 | 引脚 | 方向 | 说明 |
| --- | --- | --- | --- |
| 光照传感器 | PA0（ADC1_CH0） | 输入 | 模拟量采集 |
| 温度传感器 | PA1（ADC1_CH1） | 输入 | 模拟量采集（当前为线性估计，见下文说明） |
| 舵机 PWM | PA8（TIM1_CH1） | 输出 | 50 Hz，脉宽 500–2500 μs |
| 蜂鸣器 | PA6 | 输出 | 低电平触发 |
| 状态 LED | PA7 | 输出 | 温度告警指示 |
| 告警 LED | PA11 | 输出 | 光照告警指示 |
| 模式键 KEY_MODE | PA4 | 输入 | EXTI 下降沿触发，切换工作模式 |
| 设置键 KEY_SET | PB12 | 输入 | 任务轮询 + 30 ms 消抖 |
| OLED（软件 I2C） | PB8 / PB9 | 输出 | SSD1306，地址 0x78 |
| DTU 串口 USART1 | PA9（TX）/ PA10（RX） | 通信 | 115200 bps |
| 调试串口 USART2 | PA2（TX）/ PA3（RX） | 通信 | 115200 bps，调试日志 |
| DTU 复位 | PB11 | 输出 | 未接线（悬空；初始化置低） |
| DTU 状态（RDY） | PB10 | 输入 | 未接线（悬空） |

## 通信协议

设备通过 USART1 与 DTU/边缘网关通信，文本行 + JSON 格式，以 `\r\n` 结尾。

**上行遥测（每 5 秒）**

```json
{"cmd":"dup","did":"0","times":"123456000","param":{"light":62,"temp":27,"mode":0,"sw1":0,"in1":0,"vin":33}}
```

| 字段 | 含义 |
| --- | --- |
| `light` | 光照百分比（0–100），16 点滑动平均后取整 |
| `temp` | 温度估计值（°C，取整，标定见下文说明） |
| `mode` | 0=自动，1=手动，2=设置 |
| `sw1` | 手动模式标志（0/1） |
| `in1` | 温度超阈值标志（0/1） |
| `vin` | 供电电压（V）上报 |

**下行控制指令（网关 → 设备）**

```text
MODE=AUTO      切换到自动模式
MODE=MANUAL    切换到手动模式
SERVO=1500     设置舵机脉宽（500–2500 μs，限幅）
LED=ON / OFF   控制告警 LED
```

> 当前指令解析采用子串匹配，未知指令被静默忽略；后续计划升级为结构化 JSON 指令解析。

**调试口 USART2 也可下发同样的指令**（PA2/PA3 接 USB-TTL，收到后回显 `[HOST] RX: xxx`，命令解析与 DTU 通道一致）。串口助手发送时必须勾选"发送新行"，或使用 HEX 模式在指令末尾加 `0D 0A`——固件的命令行组装只认 `\r`/`\n` 作为行结束符，不带结束符的字节会一直积压、永不解析（排查记录见提交历史 d4e52f2 之后的调试）。

## 性能指标

实测资源占用（Keil MDK-ARM 链接器 `.map` 输出）：

| 指标 | 数值 |
| --- | --- |
| Flash | 24.87 KB / 64 KB（39%） |
| RAM | 15.38 KB / 20 KB（77%），余量约 4.6 KB |
| 任务栈 | KeyTask 512 B、Sensor/Display/Control 各 1 KB、通信 2 KB |
| 采样周期 | 1 s（16 点滑动平均） |
| 遥测周期 | 5 s |
| 舵机 PWM | 50 Hz，500–2500 μs |

> 任务栈水位（`uxTaskGetStackHighWaterMark`，[STACK] 日志每 5 s 输出一次）以板上实测值为准。

## 已知说明（诚实标注）

- **温度标定**：当前 `temp` 是 ADC 读数的线性映射（`50 - raw/4095*50`），**不是标定过的温度传感器读数**。如需可信温度，请接入真实温度传感器（DS18B20 或热敏电阻 + Steinhart-Hart 标定）后更新换算函数。
- **DTU 连接检测**：无硬件断连检测——RDY（PB10）/RST（PB11）引脚未接线，原"断连自动复位"逻辑因恒判定在线而不可达，属死代码已删除。DTU 链路健康由设备侧 5 s 心跳上报、网关/平台侧 15 s 新鲜度超时判定。
- **DMA**：CubeMX 中配置了 ADC+DMA 通道，但当前数据路径使用轮询方式读取；DMA 路径为后续优化项。

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
4. 打开串口工具（115200 bps）连接 USART2 查看调试日志。

> 编译生成的目标文件、日志和本机 IDE 配置已通过 `.gitignore` 排除，不纳入版本控制。
