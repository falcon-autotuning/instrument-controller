-- 2D Voltage Sweep Current
-- Performs a 2D nested voltage sweep and acquires current samples via the
-- instrument-data buffered path (MEASURE_STREAM / DataBuffer).
-- The slow axis steps through slowSweepVoltages; for each slow step, the
-- fast axis is swept to its end voltage in parallel with a setBins/setSampleRate
-- configuration, then measureStream collects numFastPoints samples in one shot.

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

local function VoltageSweepCurrent2D(
  ctx: RuntimeContext,
  getters: {InstrumentTarget},
  fastSweepVoltages: {number},
  slowSweepVoltages: {number},
  fastSetter: InstrumentTarget,
  slowSetter: InstrumentTarget
): string
  local numFastPoints = #fastSweepVoltages
  local fastEndVoltage = fastSweepVoltages[numFastPoints] or 0.0

  for _, slowV in ipairs(slowSweepVoltages) do
    -- Phase 1: set slow axis voltage, sweep fast axis to end voltage, and
    -- configure stream bins/sample-rate for this fast sweep, all in parallel.
    ctx:parallel(function()
      Mock1Source1:setVoltage(slowSetter.id, slowSetter.channel, slowV)
      Mock1Source1:setVoltage(fastSetter.id, fastSetter.channel, fastEndVoltage)
      for _, getter in ipairs(getters) do
        Mock5Meter1:setSampleRate(getter.id, getter.channel, 1000)
        Mock5Meter1:setBins(getter.id, getter.channel, numFastPoints)
      end
    end)

    -- Phase 2: acquire all numFastPoints samples for this slow step via
    -- the instrument-data DataBuffer (MEASURE_STREAM).
    ctx:parallel(function()
      for _, getter in ipairs(getters) do
        Mock5Meter1:measureStream(getter.id, getter.channel)
      end
    end)
  end

  return ""
end

return { main = VoltageSweepCurrent2D }
