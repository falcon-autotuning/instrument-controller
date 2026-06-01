-- 1D Voltage Sweep Current
-- Performs a 1D voltage sweep and acquires all current samples in one shot
-- via the instrument-data buffered path (MEASURE_STREAM / DataBuffer).
-- Phase 1 (parallel): configure bins = #sweepVoltages, default sample rate,
--   and set the sweep end voltage on all setters.
-- Phase 2 (parallel): trigger measureStream on all getters to collect the
--   full buffer in one instrument-data DataBuffer transfer.

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

local function VoltageSweepCurrent(
  ctx: RuntimeContext,
  getters: {InstrumentTarget},
  sweepVoltages: {number},
  setters: {InstrumentTarget}
): string
  local numPoints = #sweepVoltages
  local sampleRate = 1000
  local endVoltage = sweepVoltages[numPoints] or 0.0

  -- Phase 1: configure instruments and set sweep end voltage in parallel
  ctx:parallel(function()
    for _, setter in ipairs(setters) do
      Mock1Source1:setVoltage(setter.id, setter.channel, endVoltage)
    end
    for _, getter in ipairs(getters) do
      Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)
      Mock5Meter1:setBins(getter.id, getter.channel, numPoints)
    end
  end)

  -- Phase 2: trigger buffered stream acquisition via instrument-data DataBuffer
  ctx:parallel(function()
    for _, getter in ipairs(getters) do
      Mock5Meter1:measureStream(getter.id, getter.channel)
    end
  end)

  return ""
end
return { main = VoltageSweepCurrent }
