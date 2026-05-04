-- Auto-generated Teal module from instrument YAML (vendor=Mock model=1 id=Source1)

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

local Mock1Source1 = {}

--- Set output voltage
--- @param id string Instrument instance identifier
--- @param channel number Voltage source channel number (1-32)
--- @param voltage any
--- @return any
function Mock1Source1:setVoltage(id: string, channel: number, voltage: any): any
  local _old_channel = channel
  if channel < 1.000000 then
    channel = 1.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  if channel > 32.000000 then
    channel = 32.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  channel = math.floor(channel)
  return context:call(id .. ':' .. tostring(channel) .. '.SET_VOLTAGE', voltage)
end

--- Measure DC voltage
--- @param id string Instrument instance identifier
--- @param channel number Voltage source channel number (1-32)
--- @return number measured_voltage
function Mock1Source1:getVoltage(id: string, channel: number): number
  local _old_channel = channel
  if channel < 1.000000 then
    channel = 1.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  if channel > 32.000000 then
    channel = 32.000000
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  channel = math.floor(channel)
  return context:call(id .. ':' .. tostring(channel) .. '.GET_VOLTAGE')
end

--- resets the mock instrument
--- @param id string Instrument instance identifier
--- @return any
function Mock1Source1:reset(id: string): any
  return context:call(id .. '.RESET')
end

return Mock1Source1
