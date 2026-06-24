-- Get_Voltage
-- Reads the stored voltage from the mock source API.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
  parallel: function(self: RuntimeContext, fn: function())
  call: function(self: RuntimeContext, method: string, ...: any)
end

local function Get_Voltage(
  ctx: RuntimeContext,
  getter: InstrumentTarget
): number
  return Mock1Source1:getVoltage(getter.id, getter.channel)
end

return { main = Get_Voltage }
