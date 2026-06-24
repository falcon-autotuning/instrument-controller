-- Get_Slope
-- Returns the hub-managed slope value for the requested getter target.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Get_Slope(
  ctx: RuntimeContext,
  getter: InstrumentTarget,
  slope: number
): number
  return slope
end

return { main = Get_Slope }
