-- Get_Slope
-- Queries the slope from the mock multimeter channel.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Get_Slope(
  ctx: RuntimeContext,
  getter: InstrumentTarget
): number
  return Mock5Meter1:getSlope(getter.id, getter.channel)
end

return { main = Get_Slope }
