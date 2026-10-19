-- Dynamic objects provided by the ISS host.
global instrument_call_stack: any
global context: any

-- Test ISA adapter. ISS supplies instrument_call_stack and context at runtime.
local function setVoltage(self: any, id: string, channel_group: string, channel: integer, voltage: number): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_VOLTAGE",
  })
  return context:call(cs, voltage)
end

local function getVoltage(self: any, id: string, channel_group: string, channel: integer): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "GET_VOLTAGE",
  })
  return context:call(cs)
end

local function reset(self: any, id: string): any
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
