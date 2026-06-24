-- Set_Trigger_Leader
-- The current mock APIs do not expose explicit trigger-leader control, so this
-- schema currently acts as a no-op acknowledgement while preserving the
-- generated contract for higher-level tests.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Set_Trigger_Leader(
  ctx: RuntimeContext,
  getter: InstrumentTarget
): string
  ctx:log("Acknowledged trigger leader selection for " .. getter.id .. ":" .. tostring(getter.channel))
  return ""
end

return { main = Set_Trigger_Leader }
