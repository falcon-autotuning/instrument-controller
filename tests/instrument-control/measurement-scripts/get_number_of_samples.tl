-- Get_Number_Of_Samples
-- Returns the hub-managed bin count for the requested getter target.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Get_Number_Of_Samples(
  ctx: RuntimeContext,
  getter: InstrumentTarget,
  numberOfSamples: number
): number
  return numberOfSamples
end

return { main = Get_Number_Of_Samples }
