<div align="center">

# 🌿 Smart Env Monitor

**基于 STM32F103C8T6 与 FreeRTOS 的室内环境智能感知与调控系统**

<p>
  <img src="https://img.shields.io/badge/MCU-STM32F103C8T6-03234B?style=flat-square&logo=stmicroelectronics&logoColor=white" alt="STM32F103C8T6">
  <img src="https://img.shields.io/badge/RTOS-FreeRTOS-6A9F43?style=flat-square&logo=freertos&logoColor=white" alt="FreeRTOS">
  <img src="https://img.shields.io/badge/Language-C-A8B9CC?style=flat-square&logo=c&logoColor=white" alt="C">
  <img src="https://img.shields.io/badge/IDE-Keil_MDK-1679A7?style=flat-square" alt="Keil MDK">
  <img src="https://img.shields.io/badge/Config-STM32CubeMX-00A3E0?style=flat-square" alt="STM32CubeMX">
</p>

环境采集 · OLED 显示 · 多任务调度 · 阈值控制 · UART/JSON 网关联动

[系统设计](#-系统设计) · [任务模型](#-freertos-任务模型) · [通信协议](#-通信协议) · [快速开始](#-快速开始)

</div>

---

## 📖 项目简介

Smart Env Monitor 是一个运行在 **STM32F103C8T6** 上的嵌入式环境监控项目。系统采集光照与温度模拟量，经滤波后在 OLED 上显示，并根据自动、手动或设置模式联动舵机、LED 与蜂鸣器。设备还可通过 UART 接入 DTU/边缘网关，实现遥测数据上报和远程控制。

> 项目重点不只是“读取传感器”，而是完整实现了从 **数据采集 → 实时调度 → 状态展示 → 执行器控制 → 网关通信** 的闭环链路。

## ✨ 功能亮点

| | 功能 | 实现方式 |
| --- | --- | --- |
| 🌡️ | 环境感知 | ADC 双通道采集光照与温度，使用 16 点滑动平均滤波降低抖动。 |
| ⚙️ | 实时调度 | 基于 FreeRTOS/CMSIS-RTOS v2 拆分采集、显示、控制、通信和按键任务。 |
| 🖥️ | 本地交互 | OLED 显示数据与模式，按键支持模式切换、阈值设置和舵机调节。 |
| 🎛️ | 自动控制 | 根据光照、温度阈值联动 PWM 舵机、状态 LED、告警 LED 与蜂鸣器。 |
| 📡 | 网关通信 | UART 定时上报 JSON 遥测数据，解析模式、舵机和 LED 控制指令。 |
| 🛡️ | 异常恢复 | 检测 DTU 连接状态，连续异常时执行硬件复位与通信重新初始化。 |

## 🧩 系统设计

<div align="center">

<img src="https://mdn.alipayobjects.com/one_clip/afts/img/6v9aSa_YC3QAAAAASuAAAAgAoEACAQFr/original" alt="Smart Env Monitor 系统架构图" width="100%">

<sub>从传感器采集、FreeRTOS 任务调度到本地执行与边缘网关联动的完整数据流</sub>

</div>

设计上采用“**周期任务 + 事件唤醒**”的组合方式：连续数据由周期任务处理，模式按键通过外部中断释放信号量，设置按键通过边沿检测与消抖处理。这样既保证控制响应，又避免所有逻辑堆积在主循环中。

## 🧵 FreeRTOS 任务模型

| 任务 | 优先级 | 运行节拍 | 职责 |
| --- | --- | --- | --- |
| `KeyTask` | High | 20 ms | 响应模式键事件，轮询设置键并完成 30 ms 消抖。 |
| `SensorTask` | AboveNormal | 1000 ms | ADC 采样、16 点滤波、数据换算与调试输出。 |
| `ControlTask` | AboveNormal | 200 ms | 根据工作模式和阈值控制舵机与 LED。 |
| `DisplayTask` | Normal | 300 ms | 在状态变化时刷新 OLED，减少重复写入。 |
| `WifiTask` | BelowNormal | 200 ms | 解析指令、检测连接并按 5 秒周期上报遥测数据。 |

任务创建、优先级和核心业务逻辑集中在 [`Core/Src/freertos.c`](smart_env_monitor/Core/Src/freertos.c)。

## 🎚️ 工作模式

| 模式 | 行为 |
| --- | --- |
| **AUTO** | 光照低于阈值时驱动舵机并点亮告警 LED；温度超过阈值时切换状态指示。 |
| **MANUAL** | 通过按键或 UART 指令调整舵机 PWM，脉宽限制在 500–2500 μs。 |
| **SET** | 分步调整光照与温度阈值，保存后回到自动模式。 |

## 📡 通信协议

设备通过 USART 与 DTU/边缘网关通信，遥测数据采用 JSON 格式。

**上行遥测示例**

```json
{
  "cmd": "dup",
  "did": "0",
  "times": "123456000",
  "param": {
    "light": 62,
    "temp": 27,
    "mode": 0,
    "sw1": 0,
    "in1": 0,
    "vin": 0
  }
}
```

**支持的下行控制指令**

```text
MODE=AUTO
MODE=MANUAL
SERVO=1500
LED=ON
LED=OFF
```

## 🔧 硬件与软件

| 类别 | 选型 / 配置 |
| --- | --- |
| MCU | STM32F103C8T6，Arm Cortex-M3 |
| RTOS | FreeRTOS，CMSIS-RTOS v2 接口 |
| 输入 | ADC 通道 0/1、模式键、设置键 |
| 输出 | OLED、PWM 舵机、状态 LED、告警 LED、蜂鸣器 |
| 通信 | USART1（DTU）、USART2（调试日志） |
| 工具链 | STM32CubeMX、Keil MDK-ARM、ST-Link |

<details>
<summary><strong>查看 STM32F103C8T6 引脚参考图</strong></summary>

<br>

![STM32F103C8T6 引脚定义](参考文档/STM32F103C8T6引脚定义.png)

</details>

## 📁 仓库结构

```text
smart-env-monitor/
├── smart_env_monitor/
│   ├── Core/           # 外设初始化、FreeRTOS 任务与业务逻辑
│   ├── Drivers/        # STM32 HAL 与 CMSIS 驱动
│   ├── Middlewares/    # FreeRTOS 中间件
│   ├── User/           # OLED、延时等自定义驱动
│   ├── MDK-ARM/        # Keil uVision 工程
│   └── *.ioc           # STM32CubeMX 配置
├── 参考文档/            # 芯片与项目参考资料
└── 现有模块列表/        # 已有硬件模块清单
```

## 🚀 快速开始

### 环境准备

- Keil MDK-ARM
- STM32F1 Device Family Pack
- STM32CubeMX（需要修改外设配置时）
- ST-Link 与串口调试工具

### 构建与烧录

1. 克隆仓库并进入工程目录。
2. 使用 Keil 打开 `smart_env_monitor/MDK-ARM/smart_env_monitor.uvprojx`。
3. 选择正确的下载器与目标芯片 `STM32F103C8T6`。
4. 编译工程并通过 ST-Link 下载到开发板。
5. 打开串口工具查看传感器、DTU 和控制日志。

```bash
git clone https://github.com/Indulge125/smart-env-monitor.git
cd smart-env-monitor/smart_env_monitor
```

## ✅ 当前完成度

- [x] ADC 环境数据采集与滑动平均滤波
- [x] FreeRTOS 多任务调度
- [x] OLED 状态显示与按键交互
- [x] 自动 / 手动 / 设置模式
- [x] 舵机 PWM、LED 与蜂鸣器控制
- [x] UART JSON 遥测与控制指令解析
- [x] DTU 异常检测与复位恢复
- [ ] 补充实物接线图、运行照片与演示视频
- [ ] 增加独立协议文档和网关联调示例

## 🗺️ 后续计划

- 将传感器、显示、通信接口进一步模块化，降低任务代码耦合。
- 增加参数持久化，使阈值与模式在重新上电后保留。
- 补充任务栈水位、运行时统计和异常日志，增强可观测性。
- 增加上位机或 Web 仪表盘演示，展示完整端到端数据链路。

## 📬 联系方式

如需交流项目、嵌入式开发或合作，可发送邮件至 [1156852317@qq.com](mailto:1156852317@qq.com)。

---

<div align="center">

如果这个项目对你有帮助，欢迎通过 Issue 提出建议。

Made with embedded C and FreeRTOS by [Indulge125](https://github.com/Indulge125)

</div>
