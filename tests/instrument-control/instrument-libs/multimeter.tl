-- Dynamic objects provided by the ISS host.
global instrument_call_stack: any
global context: any

-- Test ISA adapter. ISS supplies instrument_call_stack and context at runtime.
local function setSampleRate(self: any, id: string, channel_group: string, channel: integer, sample_rate: integer): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_SAMPLE_RATE",
  })
  return context:call(cs, sample_rate)
end

local function setBins(self: any, id: string, channel_group: string, channel: integer, bins: integer): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_BINS",
  })
  return context:call(cs, bins)
end

local function setSlope(self: any, id: string, channel_group: string, channel: integer, slope: number): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_SLOPE",
  })
  return context:call(cs, slope)
end

local function getSlope(self: any, id: string, channel_group: string, channel: integer): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "GET_SLOPE",
  })
  return context:call(cs)
end

local function setTriggerLevel(self: any, id: string, channel_group: string, channel: integer, trigger_level: number): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_TRIGGER_LEVEL",
  })
  return context:call(cs, trigger_level)
end

local function measureStream(self: any, id: string, channel_group: string, channel: integer): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "MEASURE_STREAM",
  })
  return context:call(cs)
end

local function getDatapoint(self: any, id: string, channel_group: string, channel: integer): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "GET_DATAPOINT",
  })
  return context:call(cs)
end

local function getTriggerLevel(self: any, id: string, channel_group: string, channel: integer): any
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "GET_TRIGGER_LEVEL",
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
  setSampleRate = setSampleRate,
  setBins = setBins,
  setSlope = setSlope,
  getSlope = getSlope,
  setTriggerLevel = setTriggerLevel,
  measureStream = measureStream,
  getDatapoint = getDatapoint,
  getTriggerLevel = getTriggerLevel,
  reset = reset,
}
