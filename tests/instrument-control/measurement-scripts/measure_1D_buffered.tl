-- measure_1D_buffered
-- Schema-aligned buffered 1D sweep script for DataBuffer-backed measurements.
-- The sweep setter domain comes from setVoltageDomains, while numSteps controls
-- the buffered stream length returned by MEASURE_STREAM.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local record InstrumentTarget
  id: string
  channel: number
end

local record Domain
  min: number
  max: number
end

local function resolveEndVoltage(
  setVoltageDomains: {string: Domain},
  target: InstrumentTarget
): number
  local domain = setVoltageDomains[target.id]
  if domain == nil then
    return 0.0
  end
  return domain.max
end

local function Measure1DBuffered(
  ctx: RuntimeContext,
  sampleRate: number,
  setters: {InstrumentTarget},
  setVoltageDomains: {string: Domain},
  bufferedGetters: {InstrumentTarget},
  numPoints: number,
  numSteps: number,
  bufferedSetters: {InstrumentTarget}
): string
  local bins = math.max(numPoints, numSteps)

  for _, setter in ipairs(setters) do
    Mock1Source1:setVoltage(
      setter.id,
      setter.channel,
      resolveEndVoltage(setVoltageDomains, setter)
    )
  end
  for _, setter in ipairs(bufferedSetters) do
    Mock1Source1:setVoltage(
      setter.id,
      setter.channel,
      resolveEndVoltage(setVoltageDomains, setter)
    )
  end
  for _, getter in ipairs(bufferedGetters) do
    Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)
    Mock5Meter1:setBins(getter.id, getter.channel, bins)
  end

  for _, getter in ipairs(bufferedGetters) do
    Mock5Meter1:measureStream(getter.id, getter.channel)
  end

  return ""
end

return { main = Measure1DBuffered }
