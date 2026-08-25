-- Auto-generated Teal module from instrument YAML (vendor=Mock model=5 id=Meter1)

global instrument_call_stack: any

-- Helper numeric utilities for precision handling
local function _log10(x: number): number
  return math.log(x) / math.log(10)
end

local function _round_to_sig(x: number, n: number): number
  if x == 0 then return 0 end
  local d = math.floor(_log10(math.abs(x)))
  local scale = 10 ^ (d - n + 1)
  return math.floor((x / scale) + 0.5) * scale
end

local function _round_to_multiple(x: number, step: number): number
  if step == 0 then return x end
  return math.floor((x / step) + 0.5) * step
end

local function _int_keep_significant(v: number, n: number): number
  if v == 0 then return 0 end
  local neg = v < 0
  local a = math.abs(v)
  local digits = math.floor(_log10(a)) + 1
  if digits <= n then return v end
  local factor = 10 ^ (digits - n)
  local res = math.floor((a / factor) + 0.5) * factor
  if neg then res = -res end
  return res
end

Mock5Meter1 = {}

--- Set the sample rate
--- @param id string Instrument instance identifier
--- @param channel number Voltage meter channel number (1-8)
--- @param sample_rate any
--- @return any
function Mock5Meter1:setSampleRate(id: string, channel: number, sample_rate: any): any
  local _old_channel = channel
  if channel < 1.000000 then
    channel = 1.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  if channel > 8.000000 then
    channel = 8.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  channel = math.floor(channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    command = "SET_SAMPLE_RATE",
    channel = channel,
  })
  return context:call(cs, {channel = channel, sample_rate = sample_rate})
end

--- Set the number of bins to average for each data point
--- @param id string Instrument instance identifier
--- @param channel number Voltage meter channel number (1-8)
--- @param bins any
--- @return any
function Mock5Meter1:setBins(id: string, channel: number, bins: any): any
  local _old_channel = channel
  if channel < 1.000000 then
    channel = 1.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  if channel > 8.000000 then
    channel = 8.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  channel = math.floor(channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    command = "SET_BINS",
    channel = channel,
  })
  return context:call(cs, {channel = channel, bins = bins})
end

--- Set output voltage
--- @param id string Instrument instance identifier
--- @param channel number Voltage meter channel number (1-8)
--- @return MeasureResponse stream
function Mock5Meter1:measureStream(id: string, channel: number): MeasureResponse
  local _old_channel = channel
  if channel < 1.000000 then
    channel = 1.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  if channel > 8.000000 then
    channel = 8.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  channel = math.floor(channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    command = "MEASURE_STREAM",
    channel = channel,
  })
  return context:call(cs, {channel = channel})
end

--- Measure a single data point
--- @param id string Instrument instance identifier
--- @param channel number Voltage meter channel number (1-8)
--- @return number voltage
function Mock5Meter1:getDatapoint(id: string, channel: number): number
  local _old_channel = channel
  if channel < 1.000000 then
    channel = 1.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  if channel > 8.000000 then
    channel = 8.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  channel = math.floor(channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    command = "GET_DATAPOINT",
    channel = channel,
  })
  return context:call(cs, {channel = channel})
end

--- resets the mock instrument
--- @param id string Instrument instance identifier
--- @return any
function Mock5Meter1:reset(id: string): any
  local cs = instrument_call_stack.new({
    instrument = id,
    command = "RESET",
  })
  return context:call(cs)
end

return Mock5Meter1
