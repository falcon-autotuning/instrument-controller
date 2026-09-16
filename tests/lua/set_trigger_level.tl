-- Set_Trigger_Level
-- Sets the trigger level on the mock multimeter channel.
-- @falcon.metadata
-- schema_version: 1
-- measurement: set_trigger_level
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

local function Set_Trigger_Level(
  ctx: RuntimeContext,
  getter: InstrumentTarget,
  triggerLevel: number
): string
  if getter.id ~= "Meter1" then
    ctx:error("Invalid getter id: " .. getter.id)
    return nil
  end
  Mock5Meter1:setTriggerLevel(getter.id, getter.channel, triggerLevel)
end

return { main = Set_Trigger_Level }
