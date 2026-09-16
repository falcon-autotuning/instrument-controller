-- Set_Number_Of_Samples
-- Sets the number of samples of the ADC
-- @falcon.metadata
-- schema_version: 1
-- measurement: set_number_of_samples
-- targets:
--   getter:
--     capability: bins
--     role: setting
-- @falcon.end

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Set_Number_Of_Samples(
  ctx: RuntimeContext,
  getter: InstrumentTarget,
  numberOfSamples: number
): string
  if getter.id ~= "Meter1" then
    ctx:error("Invalid getter id: " .. getter.id)
    return nil
  end
  Mock5Meter1:setBins(getter.id, getter.channel, numberOfSamples)
end

return { main = Set_Number_Of_Samples }
