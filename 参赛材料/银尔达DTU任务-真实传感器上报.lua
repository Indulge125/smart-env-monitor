function
  local tname = "ZHJ_SENSOR_BRIDGE"
  local nid = 1
  local uid = 1
  local latest = nil
  local last_send = 0

  log.info(tname, "start")

  -- Script mode: read MCU UART frames and forward them to the IoT channel.
  PronetStopProRecCh(nid)
  UartStopProRecCh(uid)

  local function trim(s)
    if s == nil then return "" end
    return string.gsub(s, "^%s*(.-)%s*$", "%1")
  end

  local function number_or(v, default)
    local n = tonumber(v)
    if n == nil then return default end
    return n
  end

  local function normalize_param(obj)
    local src = obj
    if obj.param ~= nil then
      src = obj.param
    end

    local param = {}
    param.light = number_or(src.light, 0)
    param.temp = number_or(src.temp, 0)
    param.mode = number_or(src.mode, 0)
    param.sw1 = number_or(src.sw1, 0)
    param.in1 = number_or(src.in1, 0)
    param.vin = number_or(src.vin, 33)
    return param
  end

  local function send_param(param)
    local body = {}
    body.cmd = "dup"
    body.did = "0"
    body.times = tostring(os.time()) .. "000"
    body.param = param

    local payload = json.encode(body)
    PronetSetSendCh(nid, payload)
    log.info(tname, "forward", payload)

    latest = param
    last_send = os.time()
  end

  while true do
    local raw = UartGetRecChAndDel(uid)

    if raw ~= nil and raw ~= "" then
      local line = trim(raw)
      local json_start = string.find(line, "{")
      if json_start ~= nil then
        line = string.sub(line, json_start)
      end

      local ok, obj = pcall(json.decode, line)
      if ok and obj ~= nil then
        send_param(normalize_param(obj))
      else
        log.info(tname, "non_json", line)
      end
    end

    -- Keep the cloud data fresh even if one MCU frame is missed.
    if latest ~= nil and os.time() - last_send >= 5 then
      send_param(latest)
    end

    sys.wait(100)
  end
end
