-- Set_Slope
-- The current mock source API does not expose a dedicated slope-setting
-- command, so this schema currently acts as a no-op acknowledgement while
-- preserving the generated contract for higher-level tests.

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
  ctx:log("Acknowledged slope update for " .. setter.id .. ":" .. tostring(setter.channel))
  return ""
end

return { main = Set_Slope }
