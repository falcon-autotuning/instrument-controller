-- measure_2D_buffered
-- Schema-aligned buffered 2D sweep script for DataBuffer-backed measurements.
-- Each Y step sets the slow-axis voltage, then acquires a full X-axis stream.

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
  domains: {string: Domain},
  target: InstrumentTarget
): number
  local domain = domains[target.id]
  if domain == nil then
    return 0.0
  end
  return domain.max
end

local function Measure2DBuffered(
  ctx: RuntimeContext,
  bufferedXSetters: {InstrumentTarget},
  sampleRate: number,
  bufferedGetters: {InstrumentTarget},
  bufferedYSetters: {InstrumentTarget},
  numXSteps: number,
  setYVoltageDomains: {string: Domain},
  setXVoltageDomains: {string: Domain},
  numPoints: number,
  numYSteps: number,
  setters: {InstrumentTarget}
): string
  local bins = math.max(numPoints, numXSteps)

  for _, setter in ipairs(setters) do
    Mock1Source1:setVoltage(setter.id, setter.channel, 0.0)
  end

  for _ = 1, numYSteps do
    ctx:parallel(function()
      for _, setter in ipairs(bufferedYSetters) do
        Mock1Source1:setVoltage(
          setter.id,
          setter.channel,
          resolveEndVoltage(setYVoltageDomains, setter)
        )
      end
      for _, setter in ipairs(bufferedXSetters) do
        Mock1Source1:setVoltage(
          setter.id,
          setter.channel,
          resolveEndVoltage(setXVoltageDomains, setter)
        )
      end
      for _, getter in ipairs(bufferedGetters) do
        Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)
        Mock5Meter1:setBins(getter.id, getter.channel, bins)
      end
    end)

    ctx:parallel(function()
      for _, getter in ipairs(bufferedGetters) do
        Mock5Meter1:measureStream(getter.id, getter.channel)
      end
    end)
  end

  return ""
end

return { main = Measure2DBuffered }
