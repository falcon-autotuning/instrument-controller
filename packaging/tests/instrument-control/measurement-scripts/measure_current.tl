-- Measure_Current
-- Reads a scalar current-like value from the getter channel.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
end

local function Measure_Current(
  ctx: RuntimeContext,
  sampleRate: number,
  getters: {InstrumentTarget}
): number
  local getter = getters[1]
  Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)
  return Mock5Meter1:getDatapoint(getter.id, getter.channel)
end

return { main = Measure_Current }
