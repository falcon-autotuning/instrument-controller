-- Measure_Leakage
-- Returns the requested leakage probe voltage as a scalar response.
-- @falcon.metadata
-- schema_version: 1
-- measurement: measure_leakage
-- targets:
--   getter:
--     capability: measured_voltage
--     role: input
-- @falcon.end

local record RuntimeContext
  log: function(self: RuntimeContext, msg: string)
  error: function(self: RuntimeContext, msg: string)
end

local function Measure_Leakage(
  ctx: RuntimeContext,
  getter: InstrumentTarget,
  voltage: number
): number
  ctx:log("Leakage voltage for " .. getter.id .. ": " .. tostring(voltage))
  return voltage
end

return { main = Measure_Leakage }
