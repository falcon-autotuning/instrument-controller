-- Set_Voltage
-- Sets a voltage applied to a target

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end 

local function Set_Voltage(
  ctx: RuntimeContext, 
  setVoltage: number, 
  setter: InstrumentTarget
): nil
  if setter.id ~= "Source1" then
    ctx:error("Invalid setter id: " .. setter.id)
    return nil
  end
  Mock1Source1:setVoltage(setter.channel, setVoltage)
  return nil
end
return { main = Set_Voltage }
