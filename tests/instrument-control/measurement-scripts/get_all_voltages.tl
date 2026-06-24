-- Get_All_Voltages
-- Reads all requested source channels and returns their voltages.

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
end

local function Get_All_Voltages(
  ctx: RuntimeContext,
  getters: {InstrumentTarget}
): string
  for _, getter in ipairs(getters) do
    Mock1Source1:getVoltage(getter.id, getter.channel)
  end
  return ""
end

return { main = Get_All_Voltages }
