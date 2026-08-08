# 智境 C++ 网关 Web 可视化设计

## 目标

在现有 C++ 边缘网关基础上增加一个轻量 Web 仪表盘。网关继续从 STM32 串口读取 `[SENSOR]` JSON，解析后在本机启动 HTTP 服务，浏览器访问 `http://127.0.0.1:8080` 即可查看实时温度、亮度、模式、舵机 PWM 和最近历史趋势。

## 范围

本阶段只做本机 Web 可视化，不接 MQTT、不上云、不做登录、不做数据库。目标是形成一个稳定、可复刻、能写进简历的嵌入式数据链路闭环。

## 运行方式

- 自测：`zhijing_edge_gateway.exe --self-test`
- 模拟 Web：`zhijing_edge_gateway.exe --demo-web 8080`
- 真实串口 Web：`zhijing_edge_gateway.exe COM14 115200 --web 8080`

## 数据流

```text
STM32 FreeRTOS
  -> UART [SENSOR] JSON
  -> C++ Edge Gateway
  -> HTTP API
  -> Web Dashboard
```

## HTTP 接口

- `/`：返回仪表盘页面
- `/api/latest`：返回最新传感器 JSON
- `/api/history`：返回最近一段传感器历史数据

## 成功标准

- `--self-test` 能通过解析和 JSON 格式化检查。
- `--demo-web 8080` 可以不接板子打开网页，看到模拟数据变化。
- `COM14 115200 --web 8080` 可以读取真实板子串口数据，并在网页显示真实温度和亮度。
