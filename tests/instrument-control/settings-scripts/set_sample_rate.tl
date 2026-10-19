-- Set_Sample_Rate
-- Sets the sample rate on the ADC
-- @falcon.metadata
-- schema_version: 1
-- measurement: set_sample_rate
-- targets:
--   getter:
--     capability: sample_rate
--     role: setting
-- @falcon.end

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Set_Sample_Rate(
  ctx: RuntimeContext,
  getter: InstrumentTarget,
  sampleRate: number
): string
  if getter.id ~= "Meter1" then
    ctx:error("Invalid getter id: " .. getter.id)
    return nil
  end
  Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)
end

return { main = Set_Sample_Rate }
