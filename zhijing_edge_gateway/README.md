# 智境 C++ 边缘网关

这个程序运行在 Windows PC 上，用来读取 STM32 串口输出的真实传感器数据，并提供三类能力：

- 解析 `[SENSOR]` 后面的 JSON 数据。
- 通过本地 HTTP 服务提供 Web 仪表盘和 API。
- 将真实传感器数据通过 MQTT QoS0 发布到 Broker。

当前程序不依赖第三方 C++ MQTT 库，MQTT 发布使用 Winsock 手写最小 MQTT 3.1.1 报文，便于学习和复刻。

## 1. 编译

推荐使用 Visual Studio Developer PowerShell：

```powershell
cd D:\develop\programfirst\zhijing_edge_gateway
cmake -S . -B build
cmake --build build --config Release
```

## 2. 自测

```powershell
.\build\Release\zhijing_edge_gateway.exe --self-test
```

自测会检查：

- `[SENSOR]` JSON 解析。
- 最新数据 JSON 输出。
- MQTT Remaining Length 编码。
- MQTT CONNECT 报文编码。
- MQTT PUBLISH 报文编码。

期望输出：

```text
self-test passed.
```

## 3. 不接板子测试解析

```powershell
.\build\Release\zhijing_edge_gateway.exe --demo
```

期望输出类似：

```text
Demo input: [SENSOR] {"light":74,"temp":27,"mode":0,"servo":1500}
[12:00:00] temp=27 C, light=74 %, mode=0, servo=1500 us
```

## 4. 读取真实串口

先关闭串口助手，避免 COM 口被占用，然后运行：

```powershell
.\build\Release\zhijing_edge_gateway.exe COM14 115200
```

如果串口不是 `COM14`，改成实际串口号。

程序只处理包含 `[SENSOR]` 的行，例如：

```text
[SENSOR] {"light":74,"temp":27,"mode":0,"servo":1500}
```

其他日志，例如 `[DTU] TX dup`、乱码、平台下发内容，会被忽略。

## 5. 打开 Web 仪表盘

不接板子先用模拟数据测试：

```powershell
.\build\Release\zhijing_edge_gateway.exe --demo-web 8080
```

读取真实串口并打开 Web：

```powershell
.\build\Release\zhijing_edge_gateway.exe COM14 115200 --web 8080
```

浏览器打开：

```text
http://127.0.0.1:8080
```

HTTP API：

```text
http://127.0.0.1:8080/api/latest
http://127.0.0.1:8080/api/history
```

## 6. MQTT 发布

推荐先在本机启动一个 MQTT Broker，例如 Mosquitto，端口使用默认 `1883`。

### 6.1 不接板子测试 MQTT

```powershell
.\build\Release\zhijing_edge_gateway.exe --demo-mqtt 127.0.0.1 1883 zhijing/device001/telemetry
```

### 6.2 读取真实串口并发布 MQTT

```powershell
.\build\Release\zhijing_edge_gateway.exe COM14 115200 --mqtt 127.0.0.1 1883 zhijing/device001/telemetry
```

### 6.3 Web 和 MQTT 同时开启

```powershell
.\build\Release\zhijing_edge_gateway.exe COM14 115200 --web 8080 --mqtt 127.0.0.1 1883 zhijing/device001/telemetry
```

发布主题：

```text
zhijing/device001/telemetry
```

发布内容示例：

```json
{
  "deviceId": "device001",
  "temp": 30,
  "light": 64,
  "mode": 0,
  "servo": 1500,
  "ts": 1781531000000
}
```

## 7. MQTT 验证方式

可以使用 MQTTX、Mosquitto 客户端或 Node-RED 订阅：

```text
Broker: 127.0.0.1
Port: 1883
Topic: zhijing/device001/telemetry
QoS: 0
```

如果能持续收到 `temp`、`light`、`mode`、`servo`，说明链路已经打通：

```text
STM32 -> UART -> C++ 边缘网关 -> MQTT Broker -> MQTT 客户端
```

## 8. 安全与健壮性

- **HTTP 默认只绑定 127.0.0.1**：避免内网其他设备直接访问仪表盘；需要对外开放时加 `--expose`。
- **MQTT 连接带 5 秒超时**：broker 不可达时快速失败。此前 `connect` 在 Windows 上可阻塞 20s+，会拖住整条串口采集链路。
- **MQTT 发布在独立线程 + 有界队列**：串口采集线程只入队（上限 32 条，满时丢最旧保留最新），broker 不可达时采集与 Web 展示完全不受影响，错误日志 10 秒内最多打印一条。
- **HTTP 并发连接上限 32**：超过即返回 503，防止 `detach` 线程无界增长。
- **Ctrl+C 优雅退出**：串口模式、Web、MQTT 线程均在有界时间内（最长约 5 秒）退出，串口句柄与 socket 全部释放。
- **在线状态带新鲜度判定**：板端每 5s 上报一次，超过 15s 未收到新数据时 `/api/latest` 返回 `online:false` + `stale:true` 并附最后已知值，仪表盘显示"数据超时"——设备掉线不会被误报为在线。
- **浮点温度**：`"temp":27.5` 不再被整数解析截断为 27。
- **设备标识可配置**：`--device-id device002` 覆盖上报的 `deviceId`（默认 `device001`）。

常用示例：

```powershell
# 绑定所有网卡，并自定义设备标识
.\build\Release\zhijing_edge_gateway.exe COM14 115200 --web 8080 --expose --device-id device002
```

## 9. 当前限制

- MQTT 当前只支持无账号密码、无 TLS、QoS0 发布。
- 当前没有订阅控制命令，先保证真实数据上报链路稳定。
- 如果 Broker 断开，发布线程会在下一条传感器数据到来时尝试重新连接（5 秒内快速失败）；采集与 Web 展示不受影响。
