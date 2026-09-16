-- Get_Trigger_Level
-- Queries the trigger level from the mock multimeter channel.
-- @falcon.metadata
-- schema_version: 1
-- measurement: get_trigger_level
-- targets:
--   getter:
--     capability: trigger_level
--     role: setting
-- @falcon.end

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Get_Trigger_Level(
  ctx: RuntimeContext,
  getter: InstrumentTarget
): number
  return Mock5Meter1:getTriggerLevel(getter.id, getter.channel)
end

return { main = Get_Trigger_Level }
