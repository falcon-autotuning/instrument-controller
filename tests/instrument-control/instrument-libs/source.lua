-- Test ISA adapter. ISS supplies instrument_call_stack and context at runtime.
local function setVoltage(self, id, channel_group, channel, voltage)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_VOLTAGE",
  })
  return context:call(cs, voltage)
end

local function getVoltage(self, id, channel_group, channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "GET_VOLTAGE",
  })
  return context:call(cs)
end

local function reset(self, id)
  local cs = instrument_call_stack.new({
    instrument = id,
    command = "RESET",
  })
  return context:call(cs)
end

return {
  setVoltage = setVoltage,
  getVoltage = getVoltage,
  reset = reset,
}
