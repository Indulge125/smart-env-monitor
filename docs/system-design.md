# 智境系统设计文档

> 用户向文档：描述「智境」室内环境监控系统从**传感器 → 固件 → 边缘网关 → 仪表盘 → 云平台**的完整设计与数据流。配套代码见 `smart_env_monitor/`、`zhijing_edge_gateway/`、`demo_dashboard/`。

## 1. 系统概览

```
┌────────────────────────── 下位机（现场） ──────────────────────────┐
│ 光照/温度传感器 ─▶ STM32F103C8T6 固件（FreeRTOS）                │
│    ▲ 按键 / 舵机 / LED / 蜂鸣器 / OLED                            │
└──────────────┬───────────────────────────────────┬─────────────────┘
               │ USART1 (115200) JSON              │ USART2 (115200) 调试
               ▼                                   ▼
        4G DTU（银尔达 M100M-C2）          C++ 边缘网关（zhijing_edge_gateway）
               │ MQTT                          │ HTTP /api/latest、/api/history
               ▼                               ▼
        云平台（银尔达 IoT）               Web 仪表盘（gateway 内置 / demo_dashboard）
```

- **下位机固件**：采集环境数据 → 滤波 → OLED 显示 + 阈值控制 → 上报 JSON、解析控制指令。
- **4G DTU**：把 UART 遥测帧经 MQTT 转发到银尔达 IoT 平台（脚本见 `参赛材料/银尔达DTU任务-真实传感器上报.lua`）。
- **边缘网关**：监听调试串口解析传感器帧，缓存最近 120 条历史，提供 HTTP API 与内置 Web 页面，可选 MQTT 二次上报。
- **演示仪表盘**：`demo_dashboard` 用 Web Serial 直连串口；网关内置页面走 HTTP。

## 2. 下位机固件设计（smart_env_monitor）

### 2.1 FreeRTOS 任务模型

| 任务 | 优先级 | 节拍 | 职责 |
| --- | --- | --- | --- |
| KeyTask | High | 20 ms | 模式键（EXTI 信号量）+ 设置键轮询与消抖 |
| SensorTask | AboveNormal | 1000 ms | ADC 采样、16 点滑动平均、换算与调试输出 |
| ControlTask | AboveNormal | 200 ms | 按模式/阈值驱动舵机与 LED |
| DisplayTask | Normal | 300 ms | 状态变化时刷新 OLED |
| DtuTask（通信） | BelowNormal | 200 ms | 解析指令、按 5 s 周期上报遥测 |

任务间通过 `volatile` 全局共享最新状态（`g_temp_value` / `g_light_value` / `g_work_mode` / `g_servo_pulse`）；按键事件经二进制信号量 + 轮询边沿检测进入 KeyTask。

### 2.2 数据流

```
ADC 轮询 → SensorFilter(16点滑动平均) → 工程值换算 → 全局状态
                ├─▶ DisplayTask → OLED
                ├─▶ ControlTask → PWM/LED（阈值比较）
                └─▶ DtuTask → snprintf 组 JSON → USART1 → DTU
```

### 2.3 通信协议

上行 `dup` JSON（5 s 周期）与下行指令（`MODE=/SERVO=/LED=`）格式见 [smart_env_monitor/README.md](../smart_env_monitor/README.md)「通信协议」一节。

## 3. 边缘网关设计（zhijing_edge_gateway）

- 单文件 C++17，**零第三方依赖**（手写 Winsock HTTP、MQTT 3.1.1 报文、串口读写、JSON 扫描）。
- 线程模型：主线程串口循环（行缓冲 ≤1024 字符）+ HTTP 线程（每连接一线程）。
- 数据存储：`SensorStore` 互斥锁保护，保留最近 120 条历史。
- 接口：`GET /`（内置仪表盘）、`GET /api/latest`、`GET /api/history`。
- MQTT：QoS 0，可选按数据事件向 broker 发布。
- 自测：`--self-test` 覆盖串口解析、JSON 字段、MQTT 报文编码。

> 已知限制（详见 `zhijing_edge_gateway/README.md`）：无鉴权/TLS、MQTT 仅 QoS 0、不支持订阅。

## 4. 设计取舍与演进方向

| 方面 | 当前实现 | 演进方向 |
| --- | --- | --- |
| 调试串口互斥 | `xHuart2Mutex` 互斥量保护，输出不交错 | —— |
| 串口接收 | USART1/USART2 双通道环形缓冲：ISR 只写字节、任务组行消费，溢出/重挂失败计数可观测 | DMA + 空闲中断 |
| 可靠性 | IWDG 看门狗（约 2 s 超时，寄存器级），任务死循环/故障自动硬件复位；`Error_Handler` 打印后等复位 | 按最坏耗时复核喂狗位置 |
| 可观测性 | `[STACK]` 栈水位每 5 s 实测打印；`[DTU]/[HOST] RX lines/overflow/arm_fail` 统计 | 远程日志通道 |
| 命令接收 | 行结束符（`\r`/`\n`）组帧，连续指令不丢帧 | 结构化 JSON 指令 |
| 温度数据 | ADC 线性估计（未标定） | 接入真实温度传感器并标定 |
| 任务间通信 | `volatile` 全局共享最新状态 | 按消息语义改用队列 |
| 网关 MQTT | 连接超时 + 独立发布线程 + 有界队列 | 鉴权/TLS、订阅下行 |
| 测试 | 网关 `--self-test` + CI；固件 gcc 编译门禁 CI | 固件逻辑抽纯函数层 + 主机侧单测 |

## 5. 已知限制与诚实说明

- 温度与光照当前是 ADC 原始值线性换算，**未经过传感器标定**，仅用于演示相对变化。
- DTU 无硬件断连检测（RDY/RST 引脚 PB10/PB11 未接线）：链路健康由设备 5 s 心跳上报 + 网关/平台侧 15 s 新鲜度超时判定。
- 演示仪表盘的 Web Serial 路径要求浏览器处于安全上下文（localhost/HTTPS）。
- 网关 MQTT 无账号密码/TLS，仅 QoS 0 发布。
