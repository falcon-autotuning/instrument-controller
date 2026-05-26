-- 1D Gaussian Noise
-- Performs a 1D get/set measurement sweep using the non-buffered path.
-- For each voltage in sweepVoltages: sets the voltage on all setters, then
-- acquires one datapoint from each getter via GET_DATAPOINT.

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

local function Gaussian1D(
  ctx: RuntimeContext,
  getters: {InstrumentTarget},
  sweepVoltages: {number},
  setters: {InstrumentTarget}
): string
  for _, voltage in ipairs(sweepVoltages) do
    for _, setter in ipairs(setters) do
      source:setVoltage(setter.id, setter.channel, voltage)
    end
    for _, getter in ipairs(getters) do
      multimeter:getDatapoint(getter.id, getter.channel)
    end
  end
  return ""
end
return { main = Gaussian1D }
