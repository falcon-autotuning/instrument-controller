-- Set_Voltage
-- Sets a voltage applied to a target
-- @falcon.metadata
-- schema_version: 1
-- measurement: set_voltage
-- targets:
--   setter:
--     capability: voltage
--     role: output
-- @falcon.end

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end 

local function Set_Voltage(
  ctx: RuntimeContext,
  setter: InstrumentTarget,
  setVoltage: number 
): string
  if setter.id ~= "Source1" then
    ctx:error("Invalid setter id: " .. setter.id)
    return nil
  end
  Mock1Source1:setVoltage(setter.id, setter.channel, setVoltage)
end
return { main = Set_Voltage }
