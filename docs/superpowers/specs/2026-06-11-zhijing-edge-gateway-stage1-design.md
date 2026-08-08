# 智境 C++ 边缘网关第一阶段设计

## 目标

第一阶段只完成一个可复刻、可验证的 C++ 边缘网关最小版本：在 Windows 上打开 STM32 当前使用的串口，持续读取文本数据，提取 `[SENSOR]` 后面的 JSON，并输出解析后的温度、亮度、模式和舵机 PWM。

## 非目标

本阶段不接入 MQTT，不做数据库，不做 Web 仪表盘，不修改 STM32 固件，也不触碰 `smart_env_monitor - 副本`。

## 数据来源

STM32 通过调试串口持续输出类似数据：

```text
[SENSOR] {"light":74,"temp":27,"mode":0,"servo":1500}
```

网关只信任 `[SENSOR]` 行作为真实传感器数据来源，忽略 `[DTU]`、平台下发、乱码和其他调试文本。

## 第一阶段组件

- `main.cpp`：程序入口，读取命令行参数，启动串口读取循环。
- `SerialPort`：基于 Windows API 打开 COM 口并逐字节读取。
- `SensorParser`：从串口行中提取 JSON，并解析 `light`、`temp`、`mode`、`servo`。
- `SensorData`：保存一条传感器数据。

## 运行方式

默认使用：

```text
zhijing_edge_gateway.exe COM14 115200
```

如果串口号变化，可以改成：

```text
zhijing_edge_gateway.exe COM3 115200
```

## 成功标准

当 STM32 串口持续输出 `[SENSOR]` 数据时，C++ 程序应持续显示类似：

```text
temp=27 C, light=74 %, mode=0, servo=1500 us
```

如果串口断开、串口号错误、JSON 不完整或字段缺失，程序应输出可理解的错误或忽略该行，不能崩溃。
