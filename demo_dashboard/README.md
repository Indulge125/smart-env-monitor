# 智慧环境监测演示台（Web Serial）

基于浏览器 **Web Serial API** 的实时仪表盘，直接从 STM32 的串口读取传感器数据并绘图，无需安装任何上位机软件。

> 它是本项目两种仪表盘实现之一：本页面走 **Web Serial 直连串口**；`zhijing_edge_gateway` 还内置一个 **HTTP API + Web 页面** 的实现。两者解析相同的数据帧格式。

## 功能

- 连接/断开串口，动态设置波特率（默认 115200）。
- 实时解析 `[SENSOR]` 与 DTU JSON 数据帧（`light` / `temp` / `mode` / `servo`）。
- 温度、光照趋势图（Canvas 2D 手绘，无第三方图表库）。
- 连接诊断面板：串口行数、JSON 解析数、错误数，帮助排查接线问题。
- 数据刷新间隔可调（3–120 s）。

## 使用方式

Web Serial API 需要**安全上下文**：`localhost` 或 HTTPS。直接用 `file://` 打开 `index.html` 时浏览器不会暴露 `navigator.serial`。

```bash
# 在 demo_dashboard/ 目录下起一个静态服务
python -m http.server 8000
```

然后浏览器访问 `http://localhost:8000`，点击「连接串口」，选择 STM32 对应的 COM 口，波特率 115200。

## 数据帧格式

固件每 5 秒在 USART2 输出一行调试数据：

```
[SENSOR] {"light":62,"temp":27,"mode":0,"servo":1500}
```

同时 DTU 遥测帧（`dup` JSON）也可被本页面解析，字段含义见 [smart_env_monitor/README.md](../smart_env_monitor/README.md) 通信协议一节。

## 接线提示

- 串口必须连接到 **USART2（PA2/PA3，调试串口）**，接线建议写在 `index.html` 的诊断面板中。
- 模块需由 USB 转 TTL（CH340）供电，注意共地。

## 说明

- 本项目为零依赖原生 HTML/CSS/JS，无需构建。
- 仅支持串口直连场景；如需远程监控，请使用 `zhijing_edge_gateway` 的 HTTP 仪表盘。
