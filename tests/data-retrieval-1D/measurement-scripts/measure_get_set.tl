-- Measure_Get_Set
-- Provides a context for scripts that perform DC get/set operations. It allows setting voltages on specified instruments and reading values from others, supporting batch operations, parallel execution, and logging. This context is typically used for scripts that need to set outputs and acquire data points from instruments in a coordinated way.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end 

local function Measure_Get_Set(
  ctx: RuntimeContext,
  getters: {InstrumentTarget},
  numPoints: number,
  sampleRate: number,
  setVoltages: {string: number},
  setters: {InstrumentTarget}
): string
  -- parallel setting
  ctx:parallel(function()
    for setter in setters do
      local voltage = setVoltages[setter.id]
      if voltage == nil then
        ctx:error("No voltage specified for setter id: " .. setter.id)
        return nil
      end
      if setter.id ~= "Source1" then
        ctx:error("Invalid setter id: " .. setter.id)
        return nil
      end
      Mock1Source1:setVoltage(setter.channel, voltage)
    end
    for getter in getters do 
      Mock5Meter1:setSampleRate(getter.channel, sampleRate)
      Mock5Meter1:setBins(getter.channel, numPoints)
    end
  end)
  -- parallel getting
  ctx:parallel(function()
    for getter in getters do
      Mock5Meter1:measureStream(getter.channel)
    end
  end)
  return ""
end
return { main = Measure_Get_Set }
