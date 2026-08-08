#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct SensorData {
    int light = 0;
    float temp = 0.0f;  /* 支持小数温度，避免 strtol 把 27.5 截断成 27 */
    int mode = 0;
    int servo = 0;
    long long timestampMs = 0;
};

struct MqttConfig {
    std::string host;
    unsigned short port = 1883;
    std::string topic;
    std::string clientId = "zhijing-edge-gateway";
};

/* 设备标识，默认 device001；可用 --device-id 覆盖 */
static std::string g_deviceId = "device001";

/* 在线判定窗口：板端 5s 上报一次，超过 3 个周期（15s）没新数据视为掉线 */
static constexpr long long kOnlineFreshMs = 15000;

static long long currentTimeMs();   /* 前向声明：SensorStore 新鲜度判定用 */

/* 新鲜度判定：超过 kOnlineFreshMs 未收到新上报即视为掉线（纯函数，可单测） */
static bool isStale(const SensorData& data, long long nowMs) {
    return nowMs - data.timestampMs > kOnlineFreshMs;
}

static std::string sensorDataToJson(const SensorData& data, bool includeOnline) {
    std::ostringstream out;
    out << "{";
    if (includeOnline) {
        out << R"("online":true,)";
    }
    out << R"("deviceId":")" << g_deviceId << R"(",)"
        << R"("temp":)" << data.temp << ","
        << R"("light":)" << data.light << ","
        << R"("mode":)" << data.mode << ","
        << R"("servo":)" << data.servo << ","
        << R"("ts":)" << data.timestampMs;
    out << "}";
    return out.str();
}

class SensorStore {
public:
    void update(const SensorData& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = data;
        history_.push_back(data);
        if (history_.size() > 120) {
            history_.erase(history_.begin());
        }
    }

    std::string latestJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!latest_) {
            return R"({"online":false,"message":"等待传感器数据"})";
        }
        if (isStale(*latest_, currentTimeMs())) {
            return staleJson(*latest_);
        }
        return dataToJson(*latest_, true);
    }

    std::string historyJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < history_.size(); ++i) {
            if (i != 0) {
                out << ",";
            }
            out << dataToJson(history_[i], false);
        }
        out << "]";
        return out.str();
    }

private:
    static std::string dataToJson(const SensorData& data, bool includeOnline) {
        return sensorDataToJson(data, includeOnline);
    }

    /* 数据超时（设备掉线）时的诚实表达：online:false + stale:true，
       仍附最后已知值，供 API 调用方做降级展示 */
    static std::string staleJson(const SensorData& data) {
        std::ostringstream out;
        out << R"({"online":false,"stale":true,)"
            << R"("deviceId":")" << g_deviceId << R"(",)"
            << R"("temp":)" << data.temp << ","
            << R"("light":)" << data.light << ","
            << R"("mode":)" << data.mode << ","
            << R"("servo":)" << data.servo << ","
            << R"("ts":)" << data.timestampMs
            << R"(,"message":"数据超时：超过15秒未收到新上报"})";
        return out.str();
    }

    mutable std::mutex mutex_;
    std::optional<SensorData> latest_;
    std::vector<SensorData> history_;
};

static SensorStore g_store;
static std::atomic<bool> g_running{true};
static std::atomic<int> g_activeHttpClients{0};
static SOCKET g_httpServerSocket = INVALID_SOCKET;
static constexpr int kMaxHttpClients = 32;      /* 并发 HTTP 连接上限，防止 detach 线程无界增长 */
static bool g_bindLoopback = true;              /* HTTP 默认只绑 127.0.0.1；--expose 开放内网 */

/* Ctrl+C / 关闭控制台窗口：置停止标志并关闭监听 socket，使 accept 立即返回 */
static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_running = false;
        if (g_httpServerSocket != INVALID_SOCKET) {
            closesocket(g_httpServerSocket);
            g_httpServerSocket = INVALID_SOCKET;
        }
        return TRUE;
    }
    return FALSE;
}

static long long currentTimeMs() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

static void printUsage(const char* exeName) {
    std::cout << "Usage:\n"
              << "  " << exeName << " --demo\n"
              << "  " << exeName << " --self-test\n"
              << "  " << exeName << " --demo-web 8080\n"
              << "  " << exeName << " --demo-mqtt 127.0.0.1 1883 zhijing/device001/telemetry\n"
              << "  " << exeName << " COM14 115200\n"
              << "  " << exeName << " COM14 115200 --web 8080\n"
              << "  " << exeName << " COM14 115200 --mqtt 127.0.0.1 1883 zhijing/device001/telemetry\n"
              << "  " << exeName << " COM14 115200 --web 8080 --mqtt 127.0.0.1 1883 zhijing/device001/telemetry\n"
              << "  " << exeName << " COM14 115200 --web 8080 --expose --device-id device002\n\n"
              << "Options:\n"
              << "  --expose         bind HTTP to all interfaces (default: 127.0.0.1 only)\n"
              << "  --device-id ID   override reported device id (default: device001)\n\n"
              << "Web dashboard (default):\n"
              << "  http://127.0.0.1:8080\n";
}

static void appendMqttString(std::vector<uint8_t>& out, const std::string& text) {
    out.push_back(static_cast<uint8_t>((text.size() >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(text.size() & 0xFF));
    out.insert(out.end(), text.begin(), text.end());
}

static std::vector<uint8_t> encodeMqttRemainingLength(size_t length) {
    std::vector<uint8_t> encoded;
    do {
        uint8_t byte = static_cast<uint8_t>(length % 128);
        length /= 128;
        if (length > 0) {
            byte |= 0x80;
        }
        encoded.push_back(byte);
    } while (length > 0);
    return encoded;
}

static std::vector<uint8_t> makeMqttConnectPacket(const std::string& clientId, uint16_t keepAliveSeconds) {
    std::vector<uint8_t> variableAndPayload;
    appendMqttString(variableAndPayload, "MQTT");
    variableAndPayload.push_back(0x04);
    variableAndPayload.push_back(0x02);
    variableAndPayload.push_back(static_cast<uint8_t>((keepAliveSeconds >> 8) & 0xFF));
    variableAndPayload.push_back(static_cast<uint8_t>(keepAliveSeconds & 0xFF));
    appendMqttString(variableAndPayload, clientId);

    std::vector<uint8_t> packet;
    packet.push_back(0x10);
    const auto remainingLength = encodeMqttRemainingLength(variableAndPayload.size());
    packet.insert(packet.end(), remainingLength.begin(), remainingLength.end());
    packet.insert(packet.end(), variableAndPayload.begin(), variableAndPayload.end());
    return packet;
}

static std::vector<uint8_t> makeMqttPublishPacket(const std::string& topic, const std::string& payload) {
    std::vector<uint8_t> variableAndPayload;
    appendMqttString(variableAndPayload, topic);
    variableAndPayload.insert(variableAndPayload.end(), payload.begin(), payload.end());

    std::vector<uint8_t> packet;
    packet.push_back(0x30);
    const auto remainingLength = encodeMqttRemainingLength(variableAndPayload.size());
    packet.insert(packet.end(), remainingLength.begin(), remainingLength.end());
    packet.insert(packet.end(), variableAndPayload.begin(), variableAndPayload.end());
    return packet;
}

static bool sendBytes(SOCKET socketHandle, const std::vector<uint8_t>& bytes) {
    const char* data = reinterpret_cast<const char*>(bytes.data());
    int remaining = static_cast<int>(bytes.size());
    while (remaining > 0) {
        const int sent = send(socketHandle, data, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        remaining -= sent;
    }
    return true;
}

/* MQTT TCP 连接与 CONNACK 等待的超时（ms），避免 broker 不可达时阻塞整条采集链路 */
static constexpr DWORD kMqttConnectTimeoutMs = 5000;
static constexpr DWORD kMqttSocketTimeoutMs = 5000;

/* 非阻塞 connect + select 超时：Winsock 的阻塞 connect 在目标不可达时可卡 20s+ */
static bool connectWithTimeout(SOCKET s, const sockaddr* addr, int addrLen, DWORD timeoutMs) {
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    int rc = ::connect(s, addr, addrLen);
    if (rc == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            u_long blocking = 0;
            ioctlsocket(s, FIONBIO, &blocking);
            return false;
        }

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(s, &writeSet);
        timeval tv{static_cast<long>(timeoutMs / 1000), static_cast<long>((timeoutMs % 1000) * 1000)};
        const int sel = select(0, nullptr, &writeSet, nullptr, &tv);
        if (sel <= 0) {
            u_long blocking = 0;
            ioctlsocket(s, FIONBIO, &blocking);
            return false;
        }

        int soError = 0;
        int optLen = sizeof(soError);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &optLen);
        if (soError != 0) {
            u_long blocking = 0;
            ioctlsocket(s, FIONBIO, &blocking);
            return false;
        }
    }

    u_long blocking = 0;
    ioctlsocket(s, FIONBIO, &blocking);
    return true;
}

/* MQTT 失败日志限频：broker 长时间不可达时不刷屏（10s 最多一条） */
static std::atomic<long long> g_lastMqttErrorLogMs{0};
static constexpr long long kMqttErrorLogIntervalMs = 10000;

static void logMqttErrorRateLimited(const std::string& message) {
    const long long now = currentTimeMs();
    long long last = g_lastMqttErrorLogMs.load(std::memory_order_relaxed);
    if (now - last < kMqttErrorLogIntervalMs) {
        return;
    }
    if (g_lastMqttErrorLogMs.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        std::cerr << message << "\n";
    }
}

/* 发布队列上限：满时丢最旧保留最新——遥测只需要最新值 */
static constexpr size_t kMqttQueueMax = 32;

/* MQTT 发布跑在独立线程：串口采集线程只入队，绝不被 broker 阻塞。
   broker 不可达时，串口读取、Web 展示完全不受影响（历史问题：
   publish 在采集循环里同步调用，连接失败一次就卡 5s，串口缓冲溢出丢数据）。 */
class MqttPublisher {
public:
    explicit MqttPublisher(MqttConfig config) : config_(std::move(config)) {
        worker_ = std::thread(&MqttPublisher::workerLoop, this);
    }

    ~MqttPublisher() {
        stop();
    }

    /* 异步发布：入队即返回，永不阻塞调用方 */
    void publish(const SensorData& data) {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (queue_.size() >= kMqttQueueMax) {
                queue_.pop_front();
            }
            queue_.push_back(data);
        }
        queueCv_.notify_one();
    }

    /* 同步发布（--demo-mqtt 验证用）：阻塞到出结果 */
    bool publishSync(const SensorData& data) {
        return publishToBroker(data);
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stop_ = true;
        }
        queueCv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();   /* 即使正在 connect（最长 5s），join 也有界 */
        }
    }

private:
    void workerLoop() {
        for (;;) {
            SensorData item;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCv_.wait_for(lock, std::chrono::seconds(1), [this] {
                    return stop_ || !queue_.empty();
                });
                if (queue_.empty()) {
                    if (stop_) {
                        return;
                    }
                    continue;   /* 超时唤醒，继续等待 */
                }
                item = queue_.front();
                queue_.pop_front();
            }
            publishToBroker(item);
        }
    }

    bool publishToBroker(const SensorData& data) {
        if (!connectBroker()) {
            logMqttErrorRateLimited("MQTT 连接失败（" + config_.host + ":" +
                                    std::to_string(config_.port) + "），下一条数据到来时自动重试");
            return false;
        }

        const std::string payload = sensorDataToJson(data, false);
        const auto packet = makeMqttPublishPacket(config_.topic, payload);
        if (!sendBytes(socket_, packet)) {
            logMqttErrorRateLimited("MQTT PUBLISH 发送失败，将自动重连");
            close();
            return false;
        }

        std::cout << "MQTT publish: " << payload << "\n";
        return true;
    }

    bool connectBroker() {
        if (connected_) {
            return true;
        }

        if (WSAStartup(MAKEWORD(2, 2), &wsaData_) != 0) {
            return false;
        }
        wsaStarted_ = true;

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* result = nullptr;
        const std::string portText = std::to_string(config_.port);
        if (getaddrinfo(config_.host.c_str(), portText.c_str(), &hints, &result) != 0) {
            close();
            return false;
        }

        for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
            socket_ = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (socket_ == INVALID_SOCKET) {
                continue;
            }

            if (connectWithTimeout(socket_, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen), kMqttConnectTimeoutMs)) {
                break;
            }

            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
        freeaddrinfo(result);

        if (socket_ == INVALID_SOCKET) {
            close();
            return false;
        }

        DWORD rcvTimeout = kMqttSocketTimeoutMs;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));

        const auto packet = makeMqttConnectPacket(config_.clientId, 60);
        if (!sendBytes(socket_, packet)) {
            close();
            return false;
        }

        uint8_t ack[4]{};
        const int received = recv(socket_, reinterpret_cast<char*>(ack), sizeof(ack), 0);
        if (received < 4 || ack[0] != 0x20 || ack[1] != 0x02 || ack[3] != 0x00) {
            close();
            return false;
        }

        connected_ = true;
        std::cout << "MQTT connected: " << config_.host << ":" << config_.port
                  << ", topic=" << config_.topic << "\n";
        return true;
    }

    void close() {
        connected_ = false;
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
        if (wsaStarted_) {
            WSACleanup();
            wsaStarted_ = false;
        }
    }

    MqttConfig config_;
    WSADATA wsaData_{};
    SOCKET socket_ = INVALID_SOCKET;
    bool wsaStarted_ = false;
    bool connected_ = false;

    std::thread worker_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<SensorData> queue_;
    bool stop_ = false;
};

static std::optional<int> parseIntField(const std::string& json, const std::string& name) {
    const std::string key = "\"" + name + "\"";
    const size_t keyPos = json.find(key);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const size_t colonPos = json.find(':', keyPos + key.size());
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }

    size_t valuePos = colonPos + 1;
    while (valuePos < json.size() && (json[valuePos] == ' ' || json[valuePos] == '\t' || json[valuePos] == '"')) {
        ++valuePos;
    }

    char* endPtr = nullptr;
    const long value = std::strtol(json.c_str() + valuePos, &endPtr, 10);
    if (endPtr == json.c_str() + valuePos) {
        return std::nullopt;
    }

    return static_cast<int>(value);
}

/* 浮点 JSON 字段解析：保留小数（如 "temp":27.5），用于温度等模拟量 */
static std::optional<double> parseFloatField(const std::string& json, const std::string& name) {
    const std::string key = "\"" + name + "\"";
    const size_t keyPos = json.find(key);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const size_t colonPos = json.find(':', keyPos + key.size());
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }

    size_t valuePos = colonPos + 1;
    while (valuePos < json.size() && (json[valuePos] == ' ' || json[valuePos] == '\t' || json[valuePos] == '"')) {
        ++valuePos;
    }

    char* endPtr = nullptr;
    const double value = std::strtod(json.c_str() + valuePos, &endPtr);
    if (endPtr == json.c_str() + valuePos) {
        return std::nullopt;
    }

    return value;
}

static std::optional<std::string> extractSensorJson(const std::string& line) {
    const size_t markerPos = line.find("[SENSOR]");
    if (markerPos == std::string::npos) {
        return std::nullopt;
    }

    const size_t jsonStart = line.find('{', markerPos);
    const size_t jsonEnd = line.rfind('}');
    if (jsonStart == std::string::npos || jsonEnd == std::string::npos || jsonEnd <= jsonStart) {
        return std::nullopt;
    }

    return line.substr(jsonStart, jsonEnd - jsonStart + 1);
}

static std::optional<SensorData> parseSensorLine(const std::string& line) {
    const auto json = extractSensorJson(line);
    if (!json.has_value()) {
        return std::nullopt;
    }

    const auto light = parseIntField(*json, "light");
    const auto temp = parseFloatField(*json, "temp");
    const auto mode = parseIntField(*json, "mode");
    const auto servo = parseIntField(*json, "servo");
    if (!light || !temp || !mode || !servo) {
        return std::nullopt;
    }

    return SensorData{*light, static_cast<float>(*temp), *mode, *servo, currentTimeMs()};
}

static std::string nowText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
    localtime_s(&local, &t);

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

static void printSensorData(const SensorData& data) {
    std::cout << "[" << nowText() << "] "
              << "temp=" << data.temp << " C, "
              << "light=" << data.light << " %, "
              << "mode=" << data.mode << ", "
              << "servo=" << data.servo << " us\n";
}

static std::string dashboardHtml() {
    return R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>智境 - 室内环境智能感知与调控系统</title>
  <style>
    * { box-sizing: border-box; }
    :root {
      --bg: #f3f6fb;
      --panel: #ffffff;
      --line: #d9e2ef;
      --text: #172334;
      --muted: #6b7b8f;
      --primary: #1d5fd1;
      --primary-soft: #eaf1ff;
      --success: #0f9960;
      --success-soft: #e8f8f1;
      --danger: #cc3d4f;
      --danger-soft: #fff0f2;
      --shadow: 0 16px 38px rgba(19, 35, 67, 0.08);
    }
    body {
      margin: 0;
      font-family: "Microsoft YaHei", Arial, sans-serif;
      background:
        radial-gradient(circle at top right, rgba(29, 95, 209, 0.08), transparent 24%),
        linear-gradient(180deg, #eef3fb 0%, var(--bg) 240px, var(--bg) 100%);
      color: var(--text);
    }
    main { max-width: 1240px; margin: 0 auto; padding: 32px 20px 48px; }
    .hero {
      background: linear-gradient(135deg, #11233e 0%, #1b3d6d 58%, #275997 100%);
      color: #fff;
      border-radius: 8px;
      padding: 28px 30px;
      box-shadow: var(--shadow);
      margin-bottom: 18px;
      display: grid;
      grid-template-columns: minmax(0, 1.8fr) minmax(320px, 1fr);
      gap: 22px;
      align-items: end;
    }
    .eyebrow {
      margin: 0 0 10px;
      color: #87b4ff;
      font-size: 13px;
      font-weight: 700;
      letter-spacing: 1px;
      text-transform: uppercase;
    }
    h1 { margin: 0 0 12px; font-size: 36px; line-height: 1.2; }
    .lead {
      margin: 0;
      max-width: 760px;
      color: rgba(255, 255, 255, 0.82);
      line-height: 1.7;
      font-size: 15px;
    }
    .hero-side {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
    }
    .hero-meta {
      background: rgba(255, 255, 255, 0.08);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 8px;
      padding: 14px 16px;
      min-height: 92px;
      backdrop-filter: blur(6px);
    }
    .hero-meta .k {
      color: rgba(255, 255, 255, 0.64);
      font-size: 13px;
      margin-bottom: 10px;
    }
    .hero-meta .v {
      font-size: 22px;
      font-weight: 700;
      line-height: 1.3;
    }
    .hero-meta .sub {
      margin-top: 8px;
      font-size: 12px;
      color: rgba(255, 255, 255, 0.72);
    }
    .summary-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 14px;
      margin-bottom: 14px;
    }
    .summary-card, .metric, .panel {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      box-shadow: 0 8px 24px rgba(19, 35, 67, 0.04);
    }
    .summary-card {
      padding: 18px 20px;
      min-height: 104px;
    }
    .summary-card .k {
      color: var(--muted);
      font-size: 14px;
      margin-bottom: 10px;
    }
    .summary-card .v {
      font-size: 24px;
      font-weight: 700;
      margin-bottom: 6px;
    }
    .summary-card .sub {
      font-size: 13px;
      color: var(--muted);
    }
    .section-title {
      margin: 0 0 12px;
      font-size: 17px;
      font-weight: 700;
      color: #24405f;
    }
    .metric-grid {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 14px;
      margin-bottom: 16px;
    }
    .metric { padding: 20px; min-height: 140px; position: relative; overflow: hidden; }
    .metric::after {
      content: "";
      position: absolute;
      width: 92px;
      height: 92px;
      right: -22px;
      top: -22px;
      border-radius: 50%;
      opacity: 0.1;
      background: currentColor;
    }
    .metric.temp { color: #d34a56; background: linear-gradient(180deg, #fff 0%, #fff8f8 100%); }
    .metric.light { color: #db8a08; background: linear-gradient(180deg, #fff 0%, #fffaf1 100%); }
    .metric.mode { color: #325ed7; background: linear-gradient(180deg, #fff 0%, #f7f9ff 100%); }
    .metric.servo { color: #087f73; background: linear-gradient(180deg, #fff 0%, #f3fbfa 100%); }
    .metric .label { color: var(--muted); font-size: 15px; margin-bottom: 26px; position: relative; z-index: 1; }
    .metric .value { font-size: 36px; font-weight: 800; margin-bottom: 8px; position: relative; z-index: 1; }
    .metric .unit, .metric .desc { color: var(--muted); position: relative; z-index: 1; }
    .panel { overflow: hidden; }
    .panel-head {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 12px;
      padding: 16px 20px;
      border-bottom: 1px solid var(--line);
      background: linear-gradient(180deg, #ffffff 0%, #fbfdff 100%);
    }
    .panel-head h2 { margin: 0; font-size: 20px; }
    .panel-head .desc { color: var(--muted); font-size: 13px; }
    .panel-body { padding: 0; }
    .double-grid {
      display: grid;
      grid-template-columns: minmax(0, 1.5fr) minmax(300px, 0.9fr);
      gap: 16px;
      margin-bottom: 16px;
    }
    canvas { display: block; width: 100%; height: 340px; background: #fff; }
    .json-wrap {
      padding: 18px 20px;
      background: linear-gradient(180deg, #0f1b2d 0%, #13243b 100%);
      min-height: 340px;
    }
    pre {
      margin: 0;
      overflow: auto;
      color: #e6edf3;
      font-size: 14px;
      line-height: 1.6;
      white-space: pre-wrap;
      word-break: break-word;
    }
    .pill {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 10px 16px;
      border-radius: 999px;
      font-size: 14px;
      font-weight: 700;
      white-space: nowrap;
    }
    .pill.online {
      color: var(--success);
      background: var(--success-soft);
      border: 1px solid #b8e6d3;
    }
    .pill.offline {
      color: var(--danger);
      background: var(--danger-soft);
      border: 1px solid #f2c4cb;
    }
    .muted { color: var(--muted); }
    .footer-note {
      margin-top: 12px;
      font-size: 13px;
      color: var(--muted);
      text-align: right;
    }
    @media (max-width: 1040px) {
      .hero,
      .double-grid { grid-template-columns: 1fr; }
      .hero-side { grid-template-columns: repeat(2, minmax(0, 1fr)); }
      .summary-grid { grid-template-columns: 1fr; }
      .metric-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }
    @media (max-width: 620px) {
      main { padding: 20px 14px 30px; }
      .hero { padding: 22px 18px; }
      h1 { font-size: 28px; }
      .hero-side,
      .metric-grid { grid-template-columns: 1fr; }
      .panel-head { align-items: flex-start; flex-direction: column; }
    }
  </style>
</head>
<body>
  <main>
    <section class="hero">
      <div>
        <p class="eyebrow">Zhijing Indoor Environment Intelligence</p>
        <h1>智境 - 室内环境智能感知与调控系统</h1>
        <p class="lead">
          基于 STM32 + FreeRTOS 的环境感知终端，联动边缘网关完成温度、光照、运行模式与舵机状态采集展示，
          构建从下位机实时采集到上位机可视化监控的完整数据链路。
        </p>
      </div>
      <div class="hero-side">
        <div class="hero-meta">
          <div class="k">设备状态</div>
          <div class="v"><span class="pill offline" id="online">等待数据</span></div>
          <div class="sub" id="updated">最近更新：--</div>
        </div>
        <div class="hero-meta">
          <div class="k">数据刷新</div>
          <div class="v">5 秒</div>
          <div class="sub">与串口输出节奏保持一致</div>
        </div>
        <div class="hero-meta">
          <div class="k">通信链路</div>
          <div class="v">UART + HTTP</div>
          <div class="sub">已预留 MQTT 扩展能力</div>
        </div>
        <div class="hero-meta">
          <div class="k">展示目标</div>
          <div class="v">真实传感数据</div>
          <div class="sub">非模拟值，直接来自板端上报</div>
        </div>
      </div>
    </section>

    <section class="summary-grid">
      <article class="summary-card">
        <div class="k">项目定位</div>
        <div class="v">室内环境监测与调控</div>
        <div class="sub">适用于宿舍、实验室、小型办公空间等场景</div>
      </article>
      <article class="summary-card">
        <div class="k">终端能力</div>
        <div class="v">采集 / 显示 / 控制</div>
        <div class="sub">温度、光照、模式切换、舵机执行与串口输出</div>
      </article>
      <article class="summary-card">
        <div class="k">上位机能力</div>
        <div class="v">边缘解析 / 可视化</div>
        <div class="sub">C++ 网关解析 JSON 数据并通过 Web 仪表盘展示</div>
      </article>
    </section>

    <h2 class="section-title">实时运行指标</h2>
    <section class="metric-grid">
      <article class="metric temp">
        <div class="label">温度</div>
        <div class="value" id="temp">--</div>
        <div class="unit">摄氏度</div>
      </article>
      <article class="metric light">
        <div class="label">光照强度</div>
        <div class="value" id="light">--</div>
        <div class="unit">百分比</div>
      </article>
      <article class="metric mode">
        <div class="label">工作模式</div>
        <div class="value" id="mode">--</div>
        <div class="desc">0 表示默认监测模式</div>
      </article>
      <article class="metric servo">
        <div class="label">舵机 PWM</div>
        <div class="value" id="servo">--</div>
        <div class="unit">微秒</div>
      </article>
    </section>

    <section class="double-grid">
      <section class="panel">
        <div class="panel-head">
          <div>
            <h2>环境趋势曲线</h2>
            <div class="desc">展示最近一段时间的温度与光照变化趋势</div>
          </div>
          <span class="muted">自动刷新：每 5 秒一次</span>
        </div>
        <div class="panel-body">
          <canvas id="chart" width="1100" height="340"></canvas>
        </div>
      </section>

      <section class="panel">
        <div class="panel-head">
          <div>
            <h2>最新上报报文</h2>
            <div class="desc">边缘网关接收到的最新 JSON 数据</div>
          </div>
        </div>
        <div class="json-wrap">
          <pre id="json">{}</pre>
        </div>
      </section>
    </section>
    <div class="footer-note">当前页面为本地边缘展示页，可继续扩展 MQTT 发布、云端存储与远程控制能力。</div>
  </main>

  <script>
    const ids = {
      temp: document.getElementById('temp'),
      light: document.getElementById('light'),
      mode: document.getElementById('mode'),
      servo: document.getElementById('servo'),
      online: document.getElementById('online'),
      updated: document.getElementById('updated'),
      json: document.getElementById('json'),
      chart: document.getElementById('chart')
    };

    function timeText(ts) {
      if (!ts) return '--';
      return new Date(ts).toLocaleTimeString();
    }

    function modeText(mode) {
      return String(mode);
    }

    function drawChart(items) {
      const canvas = ids.chart;
      const ctx = canvas.getContext('2d');
      const w = canvas.width, h = canvas.height;
      const plotH = h - 56;
      ctx.clearRect(0, 0, w, h);
      ctx.strokeStyle = '#d8e2ec';
      ctx.lineWidth = 1;
      ctx.font = '14px Microsoft YaHei';
      ctx.fillStyle = '#718096';
      ctx.textAlign = 'left';
      for (let i = 0; i <= 4; i++) {
        const y = 28 + i * (plotH / 4);
        ctx.beginPath(); ctx.moveTo(46, y); ctx.lineTo(w - 20, y); ctx.stroke();
        ctx.fillText(String(100 - i * 25), 12, y + 4);
      }
      // 右轴：温度 0-50（板端温度映射范围）。温度与光照量纲不同，
      // 若共用左轴 0-100，30℃ 会显示在 60% 高度，视觉上严重误导。
      ctx.fillStyle = '#9aa9bb';
      for (let i = 0; i <= 5; i++) {
        const y = 28 + i * (plotH / 5);
        ctx.beginPath(); ctx.moveTo(w - 20, y); ctx.lineTo(w - 26, y); ctx.stroke();
        ctx.textAlign = 'right';
        ctx.fillText(String(50 - i * 10), w - 30, y + 4);
      }
      ctx.textAlign = 'left';
      ctx.fillStyle = '#7b8ca3';
      ctx.fillText('左轴光照 0-100 · 右轴温度 0-50', 46, 18);
      if (!items.length) {
        ctx.fillText('等待真实传感器数据上报', 56, 52);
        return;
      }
      const xOf = (i) => 46 + i * ((w - 76) / Math.max(items.length - 1, 1));
      const yOfLight = (v) => 28 + (100 - Math.max(0, Math.min(100, v))) * (plotH / 100);
      const yOfTemp = (v) => 28 + (50 - Math.max(0, Math.min(50, v))) * (plotH / 50);
      function line(field, color, yOf) {
        ctx.strokeStyle = color;
        ctx.lineWidth = 3;
        ctx.beginPath();
        items.forEach((item, i) => {
          const x = xOf(i), y = yOf(item[field]);
          if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        });
        ctx.stroke();
      }
      line('light', '#dd8b00', yOfLight);
      line('temp', '#df3f4a', yOfTemp);
      ctx.fillStyle = '#df3f4a';
      ctx.beginPath(); ctx.arc(w - 190, 24, 6, 0, Math.PI * 2); ctx.fill();
      ctx.fillText('温度(右轴)', w - 178, 29);
      ctx.fillStyle = '#dd8b00';
      ctx.beginPath(); ctx.arc(w - 106, 24, 6, 0, Math.PI * 2); ctx.fill();
      ctx.fillText('光照(左轴)', w - 94, 29);
    }

    async function refresh() {
      try {
        const [latest, history] = await Promise.all([
          fetch('/api/latest').then(r => r.json()),
          fetch('/api/history').then(r => r.json())
        ]);
        ids.json.textContent = JSON.stringify(latest, null, 2);
        if (latest.online) {
          ids.online.textContent = '设备在线';
          ids.online.className = 'pill online';
          ids.temp.textContent = latest.temp;
          ids.light.textContent = latest.light;
          ids.mode.textContent = modeText(latest.mode);
          ids.servo.textContent = latest.servo;
          ids.updated.textContent = '最近更新：' + timeText(latest.ts);
        } else {
          ids.online.textContent = latest.stale ? '数据超时' : '等待数据';
          ids.online.className = 'pill offline';
          ids.updated.textContent = latest.message ? ('状态说明：' + latest.message) : '最近更新：--';
          ids.temp.textContent = '--';
          ids.light.textContent = '--';
          ids.mode.textContent = '--';
          ids.servo.textContent = '--';
        }
        drawChart(history);
      } catch (err) {
        ids.online.textContent = '连接异常';
        ids.online.className = 'pill offline';
        ids.updated.textContent = '状态说明：' + String(err);
      }
    }

    refresh();
    setInterval(refresh, 5000);
  </script>
</body>
</html>)HTML";
}

static std::string httpHeader(const std::string& contentType, size_t length, const std::string& status = "200 OK") {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "; charset=utf-8\r\n"
        << "Content-Length: " << length << "\r\n"
        << "Connection: close\r\n"
        << "Cache-Control: no-store\r\n\r\n";
    return out.str();
}

static void sendAll(SOCKET client, const std::string& text) {
    const char* data = text.c_str();
    int remaining = static_cast<int>(text.size());
    while (remaining > 0) {
        const int sent = send(client, data, remaining, 0);
        if (sent <= 0) {
            return;
        }
        data += sent;
        remaining -= sent;
    }
}

static void sendResponse(SOCKET client, const std::string& body, const std::string& contentType, const std::string& status = "200 OK") {
    sendAll(client, httpHeader(contentType, body.size(), status) + body);
}

static std::string requestPath(const std::string& request) {
    const size_t firstSpace = request.find(' ');
    if (firstSpace == std::string::npos) {
        return "/";
    }
    const size_t secondSpace = request.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos || secondSpace <= firstSpace + 1) {
        return "/";
    }
    return request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
}

static void handleHttpClient(SOCKET client) {
    struct ClientGuard {
        ~ClientGuard() { --g_activeHttpClients; }
    } guard;  /* 连接计数由 accept 循环 +1，此处负责释放 */

    char buffer[2048]{};
    const int received = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        closesocket(client);
        return;
    }

    const std::string path = requestPath(std::string(buffer, received));
    if (path == "/" || path == "/index.html") {
        sendResponse(client, dashboardHtml(), "text/html");
    } else if (path == "/api/latest") {
        sendResponse(client, g_store.latestJson(), "application/json");
    } else if (path == "/api/history") {
        sendResponse(client, g_store.historyJson(), "application/json");
    } else {
        sendResponse(client, R"({"error":"not found"})", "application/json", "404 Not Found");
    }
    closesocket(client);
}

static void runHttpServer(unsigned short port) {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "socket failed.\n";
        WSACleanup();
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(g_bindLoopback ? INADDR_LOOPBACK : INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed. Port " << port << " may be in use.\n";
        closesocket(server);
        WSACleanup();
        return;
    }

    if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed.\n";
        closesocket(server);
        WSACleanup();
        return;
    }

    g_httpServerSocket = server;
    std::cout << "Web dashboard: http://127.0.0.1:" << port << "\n";
    if (!g_bindLoopback) {
        std::cout << "  (bound to all interfaces via --expose)\n";
    }
    while (g_running) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!g_running) {
                break;  /* Ctrl+C 已关闭监听 socket，正常退出 */
            }
            continue;
        }
        if (g_activeHttpClients >= kMaxHttpClients) {
            sendResponse(client, R"({"error":"too many connections"})", "application/json", "503 Service Unavailable");
            closesocket(client);
            continue;
        }
        ++g_activeHttpClients;
        std::thread(handleHttpClient, client).detach();
    }

    closesocket(server);
    g_httpServerSocket = INVALID_SOCKET;
    WSACleanup();
}

static std::string makeWinPortName(const std::string& portName) {
    if (portName.rfind("\\\\.\\", 0) == 0) {
        return portName;
    }
    return "\\\\.\\" + portName;
}

static HANDLE openSerialPort(const std::string& portName, DWORD baudRate) {
    const std::string winPortName = makeWinPortName(portName);

    HANDLE port = CreateFileA(winPortName.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (port == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open " << portName << ". Check port number and close other serial tools.\n";
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(port, &dcb)) {
        std::cerr << "GetCommState failed.\n";
        CloseHandle(port);
        return INVALID_HANDLE_VALUE;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;

    if (!SetCommState(port, &dcb)) {
        std::cerr << "SetCommState failed. Unsupported baud rate may be used.\n";
        CloseHandle(port);
        return INVALID_HANDLE_VALUE;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(port, &timeouts);

    PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return port;
}

static int runSerialMode(const std::string& portName, DWORD baudRate, const std::optional<MqttConfig>& mqttConfig) {
    HANDLE port = openSerialPort(portName, baudRate);
    if (port == INVALID_HANDLE_VALUE) {
        return 1;
    }

    std::cout << "Connected to " << portName << " at " << baudRate << ". Waiting for [SENSOR] lines...\n";

    std::unique_ptr<MqttPublisher> mqtt;
    if (mqttConfig.has_value()) {
        mqtt = std::make_unique<MqttPublisher>(*mqttConfig);
    }

    std::string line;
    while (true) {
        if (!g_running) {
            break;   /* Ctrl+C：ReadFile 最多 50ms 返回一次，退出有界 */
        }

        char ch = 0;
        DWORD bytesRead = 0;
        if (!ReadFile(port, &ch, 1, &bytesRead, nullptr)) {
            std::cerr << "Serial read failed. Device may be disconnected.\n";
            CloseHandle(port);
            return 2;
        }

        if (bytesRead == 0) {
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (const auto data = parseSensorLine(line)) {
                g_store.update(*data);
                printSensorData(*data);
                if (mqtt) {
                    mqtt->publish(*data);   /* 只入队，不等待 broker */
                }
            }
            line.clear();
            continue;
        }

        line.push_back(ch);
        if (line.size() > 1024) {
            line.clear();
        }
    }

    CloseHandle(port);
    std::cout << "Stopped.\n";
    return 0;
}

static int runDemoMode() {
    const std::string sample = R"([SENSOR] {"light":74,"temp":27,"mode":0,"servo":1500})";
    const auto data = parseSensorLine(sample);
    if (!data) {
        std::cerr << "Demo parse failed.\n";
        return 1;
    }

    std::cout << "Demo input: " << sample << "\n";
    printSensorData(*data);
    return 0;
}

static int runSelfTest() {
    const auto data = parseSensorLine(R"([SENSOR] {"light":74,"temp":27,"mode":0,"servo":1500})");
    if (!data || data->light != 74 || data->temp != 27.0f || data->mode != 0 || data->servo != 1500) {
        std::cerr << "parser self-test failed.\n";
        return 1;
    }

    const auto floatData = parseSensorLine(R"([SENSOR] {"light":74,"temp":27.5,"mode":0,"servo":1500})");
    if (!floatData || floatData->temp != 27.5f) {
        std::cerr << "float temp parse self-test failed.\n";
        return 1;
    }

    g_store.update(*data);
    const std::string latest = g_store.latestJson();
    if (latest.find(R"("temp":27)") == std::string::npos || latest.find(R"("light":74)") == std::string::npos) {
        std::cerr << "json self-test failed: " << latest << "\n";
        return 1;
    }

    /* 在线状态新鲜度：刚更新的数据不算掉线，20s 前的旧数据必须判定为掉线 */
    const auto freshNow = currentTimeMs();
    const SensorData freshData{74, 27.0f, 0, 1500, freshNow};
    const SensorData oldData{74, 27.0f, 0, 1500, freshNow - 20000};
    if (isStale(freshData, freshNow) || !isStale(oldData, freshNow)) {
        std::cerr << "freshness self-test failed.\n";
        return 1;
    }

    const auto encoded128 = encodeMqttRemainingLength(128);
    if (encoded128 != std::vector<uint8_t>{0x80, 0x01}) {
        std::cerr << "mqtt remaining length self-test failed.\n";
        return 1;
    }

    const auto connectPacket = makeMqttConnectPacket("gw", 60);
    const std::vector<uint8_t> expectedConnect{
        0x10, 0x0E, 0x00, 0x04, 'M', 'Q', 'T', 'T',
        0x04, 0x02, 0x00, 0x3C, 0x00, 0x02, 'g', 'w'};
    if (connectPacket != expectedConnect) {
        std::cerr << "mqtt connect packet self-test failed.\n";
        return 1;
    }

    const auto publishPacket = makeMqttPublishPacket("a/b", "{}");
    const std::vector<uint8_t> expectedPublish{0x30, 0x07, 0x00, 0x03, 'a', '/', 'b', '{', '}'};
    if (publishPacket != expectedPublish) {
        std::cerr << "mqtt publish packet self-test failed.\n";
        return 1;
    }

    std::cout << "self-test passed.\n";
    return 0;
}

static int runDemoWeb(unsigned short port) {
    std::thread(runHttpServer, port).detach();

    int light = 45;
    int temp = 26;
    while (true) {
        if (!g_running) {
            break;
        }
        light += 7;
        if (light > 92) {
            light = 38;
        }
        temp = 25 + ((light / 10) % 5);
        SensorData data{light, static_cast<float>(temp), 0, 1500, currentTimeMs()};
        g_store.update(data);
        printSensorData(data);
        Sleep(5000);
    }
    return 0;
}

static int runDemoMqtt(const MqttConfig& mqttConfig) {
    MqttPublisher mqtt(mqttConfig);
    SensorData data{74, 27.0f, 0, 1500, currentTimeMs()};
    printSensorData(data);
    return mqtt.publishSync(data) ? 0 : 1;   /* 验证模式用同步发布，直接看结果 */
}

static std::optional<unsigned short> parsePort(const char* text) {
    char* endPtr = nullptr;
    const unsigned long value = std::strtoul(text, &endPtr, 10);
    if (endPtr == text || *endPtr != '\0' || value == 0 || value > 65535) {
        return std::nullopt;
    }
    return static_cast<unsigned short>(value);
}

static std::optional<DWORD> parseBaud(const char* text) {
    char* endPtr = nullptr;
    const unsigned long value = std::strtoul(text, &endPtr, 10);
    if (endPtr == text || *endPtr != '\0' || value < 300 || value > 2000000) {
        return std::nullopt;
    }
    return static_cast<DWORD>(value);
}

int main(int argc, char* argv[]) {
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    if (argc == 2 && std::string(argv[1]) == "--demo") {
        return runDemoMode();
    }

    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        return runSelfTest();
    }

    if (argc >= 3 && argc <= 4 && std::string(argv[1]) == "--demo-web") {
        const auto port = parsePort(argv[2]);
        if (!port) {
            std::cerr << "Invalid web port: " << argv[2] << "\n";
            return 1;
        }
        if (argc == 4 && std::string(argv[3]) == "--expose") {
            g_bindLoopback = false;
        }
        return runDemoWeb(*port);
    }

    if (argc == 5 && std::string(argv[1]) == "--demo-mqtt") {
        const auto mqttPort = parsePort(argv[3]);
        if (!mqttPort) {
            std::cerr << "Invalid MQTT port: " << argv[3] << "\n";
            return 1;
        }
        return runDemoMqtt(MqttConfig{argv[2], *mqttPort, argv[4]});
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string portName = argv[1];
    const auto baud = parseBaud(argv[2]);
    if (!baud) {
        std::cerr << "Invalid baud rate (expect 300-2000000): " << argv[2] << "\n";
        return 1;
    }
    const DWORD baudRate = *baud;

    std::optional<MqttConfig> mqttConfig;
    int argIndex = 3;
    while (argIndex < argc) {
        const std::string option = argv[argIndex];
        if (option == "--web") {
            if (argIndex + 1 >= argc) {
                printUsage(argv[0]);
                return 1;
            }
            const auto webPort = parsePort(argv[argIndex + 1]);
            if (!webPort) {
                std::cerr << "Invalid web port: " << argv[argIndex + 1] << "\n";
                return 1;
            }
            std::thread(runHttpServer, *webPort).detach();
            argIndex += 2;
            continue;
        }

        if (option == "--mqtt") {
            if (argIndex + 3 >= argc) {
                printUsage(argv[0]);
                return 1;
            }
            const auto mqttPort = parsePort(argv[argIndex + 2]);
            if (!mqttPort) {
                std::cerr << "Invalid MQTT port: " << argv[argIndex + 2] << "\n";
                return 1;
            }
            mqttConfig = MqttConfig{argv[argIndex + 1], *mqttPort, argv[argIndex + 3]};
            argIndex += 4;
            continue;
        }

        if (option == "--expose") {
            g_bindLoopback = false;
            argIndex += 1;
            continue;
        }

        if (option == "--device-id") {
            if (argIndex + 1 >= argc) {
                printUsage(argv[0]);
                return 1;
            }
            g_deviceId = argv[argIndex + 1];
            argIndex += 2;
            continue;
        }

        printUsage(argv[0]);
        return 1;
    }

    return runSerialMode(portName, baudRate, mqttConfig);
}
