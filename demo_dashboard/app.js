const state = {
  port: null,
  reader: null,
  buffer: "",
  samples: [],
  lastPayload: null,
  sampleCount: 0,
  lineCount: 0,
  jsonCount: 0,
  pendingPayload: null,
  renderTimer: null,
  lastRenderAt: 0,
};

const els = {
  connectBtn: document.querySelector("#connectBtn"),
  disconnectBtn: document.querySelector("#disconnectBtn"),
  clearBtn: document.querySelector("#clearBtn"),
  baudRate: document.querySelector("#baudRate"),
  refreshInterval: document.querySelector("#refreshInterval"),
  serialStatus: document.querySelector("#serialStatus"),
  lastUpdate: document.querySelector("#lastUpdate"),
  lineCount: document.querySelector("#lineCount"),
  jsonCount: document.querySelector("#jsonCount"),
  sourceStatus: document.querySelector("#sourceStatus"),
  diagMessage: document.querySelector("#diagMessage"),
  wiringHint: document.querySelector("#wiringHint"),
  tempValue: document.querySelector("#tempValue"),
  lightValue: document.querySelector("#lightValue"),
  modeValue: document.querySelector("#modeValue"),
  modeName: document.querySelector("#modeName"),
  servoValue: document.querySelector("#servoValue"),
  alarmValue: document.querySelector("#alarmValue"),
  alarmName: document.querySelector("#alarmName"),
  rssiValue: document.querySelector("#rssiValue"),
  latestJson: document.querySelector("#latestJson"),
  logList: document.querySelector("#logList"),
  chart: document.querySelector("#chart"),
};

const modeNames = {
  0: "自动",
  1: "手动",
  2: "设定",
};

function addLog(text, type = "") {
  const item = document.createElement("li");
  item.className = type;
  item.textContent = `${new Date().toLocaleTimeString()}  ${text}`;
  els.logList.prepend(item);

  while (els.logList.children.length > 80) {
    els.logList.removeChild(els.logList.lastElementChild);
  }
}

function setDiagnostic(message, hint = "") {
  els.diagMessage.textContent = message;
  if (hint) els.wiringHint.textContent = hint;
}

function normalizePayload(raw) {
  if (!raw || typeof raw !== "object") return null;
  const source = raw.param && typeof raw.param === "object" ? raw.param : raw;

  return {
    light: numberOrNull(source.light),
    temp: numberOrNull(source.temp),
    mode: numberOrNull(source.mode),
    servo: numberOrNull(source.servo),
    sw1: numberOrNull(source.sw1),
    in1: numberOrNull(source.in1),
    rssi: numberOrNull(source.rssi),
    iccid: source.iccid || raw.iccid || "",
    imei: source.imei || raw.imei || "",
    fver: source.fver || raw.fver || "",
    hver: source.hver || source.pver || raw.hver || raw.pver || "",
    receivedAt: Date.now(),
    raw,
  };
}

function numberOrNull(value) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function formatValue(value, digits = 0) {
  if (value === null || value === undefined) return "--";
  return Number(value).toFixed(digits);
}

function renderPayload(payload) {
  state.lastPayload = payload;
  state.sampleCount += 1;

  els.tempValue.textContent = formatValue(payload.temp, 0);
  els.lightValue.textContent = formatValue(payload.light, 0);
  els.modeValue.textContent = payload.mode ?? "--";
  els.modeName.textContent = modeNames[payload.mode] || "未知";
  els.servoValue.textContent = payload.servo ?? "--";
  els.alarmValue.textContent = payload.sw1 === 1 ? "ON" : "OFF";
  els.alarmName.textContent = payload.sw1 === 1 ? "已触发" : "未触发";
  els.rssiValue.textContent = payload.rssi ?? "--";
  els.lastUpdate.textContent = `更新 ${new Date(payload.receivedAt).toLocaleTimeString()} · ${state.sampleCount} 条`;
  els.latestJson.textContent = JSON.stringify(payload.raw, null, 2);
  els.sourceStatus.textContent = "真实串口";
  setDiagnostic("已解析到板子串口 JSON，页面正在按设定间隔显示真实数据", "当前串口数据有效，可以继续录制展示。");

  state.samples.push({
    t: payload.receivedAt,
    temp: payload.temp,
    light: payload.light,
  });

  if (state.samples.length > 60) state.samples.shift();
  drawChart();
}

function queuePayload(payload) {
  state.jsonCount += 1;
  els.jsonCount.textContent = String(state.jsonCount);
  state.pendingPayload = payload;

  const intervalMs = Math.max(3, Number(els.refreshInterval.value) || 5) * 1000;
  const elapsed = Date.now() - state.lastRenderAt;

  if (elapsed >= intervalMs) {
    state.lastRenderAt = Date.now();
    renderPayload(state.pendingPayload);
    state.pendingPayload = null;
    return;
  }

  if (!state.renderTimer) {
    state.renderTimer = window.setTimeout(() => {
      state.renderTimer = null;
      state.lastRenderAt = Date.now();
      if (state.pendingPayload) {
        renderPayload(state.pendingPayload);
        state.pendingPayload = null;
      }
    }, intervalMs - elapsed);
  }

  setDiagnostic("已收到真实 JSON，等待刷新间隔后更新显示", "页面会保留最新一条真实串口数据，不使用模拟数据。");
}

function parseLine(line) {
  const trimmed = line.trim();
  if (!trimmed) return;

  state.lineCount += 1;
  els.lineCount.textContent = String(state.lineCount);

  const start = trimmed.indexOf("{");
  const end = trimmed.lastIndexOf("}");

  if (start < 0 || end <= start) {
    addLog(trimmed, "");
    if (trimmed.includes("disconnected too long") || trimmed.includes("waiting for connection")) {
      setDiagnostic(
        "收到 DTU 断连日志，但没有传感器 JSON",
        "当前接的是 USART2 调试口；固件认为 PB10/DTU_RDY 未连接，所以不会输出 [DTU] TX JSON。可改接 PA9 监听上行 JSON，或先用演示数据录制。"
      );
    } else if (/^([0-9A-Fa-f]{2}\s+)+[0-9A-Fa-f]{2}$/.test(trimmed)) {
      setDiagnostic(
        "收到十六进制串口内容，但不是传感器 JSON",
        "这通常是 DTU 透明透传的 AT 或注册内容；请改接 PA2 看调试日志，或改接 PA9 监听 MCU 上行。"
      );
    } else {
      setDiagnostic("收到串口文本，但还没有 JSON 数据", "继续等待 [DTU] TX: {...}，或点击演示数据用于视频录制。");
    }
    return;
  }

  const jsonText = trimmed.slice(start, end + 1);
  try {
    const raw = JSON.parse(jsonText);
    const payload = normalizePayload(raw);
    if (!payload) {
      addLog(`无法识别: ${jsonText}`, "error");
      return;
    }
    queuePayload(payload);
    addLog(jsonText, "ok");
  } catch (error) {
    addLog(`JSON解析失败: ${jsonText}`, "error");
    setDiagnostic("发现 JSON 片段但解析失败", "请把串口日志里的完整一行发我，我再按实际格式适配解析器。");
  }
}

function consumeText(text) {
  state.buffer += text;
  const lines = state.buffer.split(/\r?\n/);
  state.buffer = lines.pop() || "";
  lines.forEach(parseLine);
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    addLog("当前浏览器不支持 Web Serial，请用新版 Chrome 或 Edge 打开 localhost 页面", "error");
    return;
  }

  try {
    const baudRate = Number(els.baudRate.value) || 115200;
    state.port = await navigator.serial.requestPort();
    await state.port.open({ baudRate });

    els.serialStatus.textContent = "串口已连接";
    els.connectBtn.disabled = true;
    els.disconnectBtn.disabled = false;
    addLog(`串口连接成功，波特率 ${baudRate}`, "ok");

    const decoder = new TextDecoderStream();
    state.port.readable.pipeTo(decoder.writable).catch(() => {});
    state.reader = decoder.readable.getReader();

    while (state.port && state.reader) {
      const { value, done } = await state.reader.read();
      if (done) break;
      if (value) consumeText(value);
    }
  } catch (error) {
    addLog(`串口连接失败: ${error.message}`, "error");
    await disconnectSerial();
  }
}

async function disconnectSerial() {
  try {
    if (state.reader) {
      await state.reader.cancel();
      state.reader.releaseLock();
    }
    if (state.port) await state.port.close();
  } catch (_) {
    // Ignore shutdown races from the browser serial stream.
  }

  state.reader = null;
  state.port = null;
  els.serialStatus.textContent = "串口未连接";
  els.connectBtn.disabled = false;
  els.disconnectBtn.disabled = true;
}

function clearData() {
  state.buffer = "";
  state.samples = [];
  state.lastPayload = null;
  state.sampleCount = 0;
  state.lineCount = 0;
  state.jsonCount = 0;
  state.pendingPayload = null;
  state.lastRenderAt = 0;
  if (state.renderTimer) window.clearTimeout(state.renderTimer);
  state.renderTimer = null;
  els.tempValue.textContent = "--";
  els.lightValue.textContent = "--";
  els.modeValue.textContent = "--";
  els.modeName.textContent = "等待";
  els.servoValue.textContent = "--";
  els.alarmValue.textContent = "--";
  els.alarmName.textContent = "未触发";
  els.rssiValue.textContent = "--";
  els.lastUpdate.textContent = "等待数据";
  els.latestJson.textContent = "{}";
  els.lineCount.textContent = "0";
  els.jsonCount.textContent = "0";
  els.sourceStatus.textContent = "未确认";
  setDiagnostic("连接后等待串口输出", "等待串口数据后自动判断。");
  els.logList.innerHTML = "";
  drawChart();
}

function drawChart() {
  const canvas = els.chart;
  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = Math.max(1, Math.floor(rect.width * dpr));
  canvas.height = Math.max(1, Math.floor(rect.height * dpr));
  ctx.scale(dpr, dpr);

  const w = rect.width;
  const h = rect.height;
  const pad = { left: 48, right: 22, top: 24, bottom: 36 };
  const plotW = w - pad.left - pad.right;
  const plotH = h - pad.top - pad.bottom;

  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#fff";
  ctx.fillRect(0, 0, w, h);

  ctx.strokeStyle = "#d9e1e8";
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let i = 0; i <= 4; i += 1) {
    const y = pad.top + (plotH / 4) * i;
    ctx.moveTo(pad.left, y);
    ctx.lineTo(w - pad.right, y);
  }
  ctx.stroke();

  ctx.fillStyle = "#6b7785";
  ctx.font = "12px Microsoft YaHei, sans-serif";
  [0, 25, 50, 75, 100].forEach((label, i) => {
    const y = pad.top + plotH - (plotH / 100) * label;
    ctx.fillText(String(label), 14, y + 4);
  });

  if (state.samples.length < 2) {
    ctx.fillStyle = "#8a96a3";
    ctx.font = "15px Microsoft YaHei, sans-serif";
    ctx.fillText("等待串口数据", pad.left, pad.top + 26);
    return;
  }

  drawSeries(ctx, state.samples, "temp", "#d33f49", pad, plotW, plotH);
  drawSeries(ctx, state.samples, "light", "#d88714", pad, plotW, plotH);
}

function drawSeries(ctx, samples, key, color, pad, plotW, plotH) {
  ctx.strokeStyle = color;
  ctx.lineWidth = 2.5;
  ctx.beginPath();

  samples.forEach((sample, index) => {
    const value = sample[key];
    if (value === null || value === undefined) return;
    const x = pad.left + (plotW * index) / Math.max(1, samples.length - 1);
    const y = pad.top + plotH - (Math.max(0, Math.min(100, value)) / 100) * plotH;
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });

  ctx.stroke();
}

els.connectBtn.addEventListener("click", connectSerial);
els.disconnectBtn.addEventListener("click", disconnectSerial);
els.clearBtn.addEventListener("click", clearData);
window.addEventListener("resize", drawChart);

drawChart();
