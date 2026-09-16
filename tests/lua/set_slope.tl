-- Set_Slope
-- Sets the slope on the mock multimeter channel.
-- @falcon.metadata
-- schema_version: 1
-- measurement: set_slope
-- targets:
--   setter:
--     capability: slope
--     role: setting
-- @falcon.end

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Set_Slope(
  ctx: RuntimeContext,
  setter: InstrumentTarget,
  slope: number
): string
  if setter.id ~= "Meter1" then
    ctx:error("Invalid setter id: " .. setter.id)
    return nil
  end
  Mock5Meter1:setSlope(setter.id, setter.channel, slope)
end

return { main = Set_Slope }
