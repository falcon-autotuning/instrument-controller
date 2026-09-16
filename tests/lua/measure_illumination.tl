-- Measure_Illumination
-- Runs a simple illumination-style readback after setting the sample rate.
-- @falcon.metadata
-- schema_version: 1
-- measurement: measure_illumination
-- targets:
--   getter:
--     capability: voltage
--     role: input
-- @falcon.end

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
end

local function Measure_Illumination(
  ctx: RuntimeContext,
  sampleRate: number,
  getters: {InstrumentTarget},
  illuminationTime: number
): number
  local getter = getters[1]
  ctx:log("Illumination time: " .. tostring(illuminationTime))
  Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)
  return Mock5Meter1:getDatapoint(getter.id, getter.channel)
end

return { main = Measure_Illumination }
