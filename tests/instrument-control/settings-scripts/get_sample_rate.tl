-- Get_Sample_Rate
-- Returns the hub-managed sample rate value for the requested getter target.
-- @falcon.metadata
-- schema_version: 1
-- measurement: get_sample_rate
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

local function Get_Sample_Rate(
  ctx: RuntimeContext,
  getter: InstrumentTarget,
  sampleRate: number
): number
  return sampleRate
end

return { main = Get_Sample_Rate }
