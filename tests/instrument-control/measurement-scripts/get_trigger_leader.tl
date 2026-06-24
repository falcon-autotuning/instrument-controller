-- Get_Trigger_Leader
-- Returns the hub-managed trigger leader selection for the requested getter.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Get_Trigger_Leader(
  ctx: RuntimeContext,
  getter: InstrumentTarget,
  triggerLeader: boolean
): boolean
  return triggerLeader
end

return { main = Get_Trigger_Leader }
