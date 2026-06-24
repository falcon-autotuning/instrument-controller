-- Ramp
-- Sets all voltages applied to targets to their requested end-state values.
-- The current mock source API does not expose a dedicated ramp primitive, so
-- this schema currently applies the final voltages directly.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function target_key(target: InstrumentTarget): string
  return target.id .. ":" .. tostring(target.channel)
end

local function Ramp(
  ctx: RuntimeContext,
  setters: {InstrumentTarget},
  setVoltages: {string: number}
): string
  for _, setter in ipairs(setters) do
    local key = target_key(setter)
    local setVoltage = setVoltages[key]
    if setVoltage == nil then
      ctx:error("Missing ramp target voltage for " .. key)
      return nil
    end
    Mock1Source1:setVoltage(setter.id, setter.channel, setVoltage)
  end
end

return { main = Ramp }
