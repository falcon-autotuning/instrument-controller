-- Get_Many_Voltages
-- Reads a set of source channels and returns their voltages.
-- @falcon.metadata
-- schema_version: 1
-- measurement: get_many_voltages
-- targets:
--   getter:
--     capability: measured_voltage
--     role: input
-- @falcon.end

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
end

local function Get_Many_Voltages(
  ctx: RuntimeContext,
  getters: {InstrumentTarget}
): string
  for _, getter in ipairs(getters) do
    Mock1Source1:getVoltage(getter.id, getter.channel)
  end
  return ""
end

return { main = Get_Many_Voltages }
