-- Test ISA adapter. ISS supplies instrument_call_stack and context at runtime.
local function setSampleRate(self, id, channel_group, channel, sample_rate)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_SAMPLE_RATE",
  })
  return context:call(cs, sample_rate)
end

local function setBins(self, id, channel_group, channel, bins)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_BINS",
  })
  return context:call(cs, bins)
end

local function setSlope(self, id, channel_group, channel, slope)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_SLOPE",
  })
  return context:call(cs, slope)
end

local function getSlope(self, id, channel_group, channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "GET_SLOPE",
  })
  return context:call(cs)
end

local function setTriggerLevel(self, id, channel_group, channel, trigger_level)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "SET_TRIGGER_LEVEL",
  })
  return context:call(cs, trigger_level)
end

local function measureStream(self, id, channel_group, channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "MEASURE_STREAM",
  })
  return context:call(cs)
end

local function getDatapoint(self, id, channel_group, channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "GET_DATAPOINT",
  })
  return context:call(cs)
end

local function getTriggerLevel(self, id, channel_group, channel)
  local cs = instrument_call_stack.new({
    instrument = id,
    group = channel_group,
    channel = channel,
    command = "GET_TRIGGER_LEVEL",
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
