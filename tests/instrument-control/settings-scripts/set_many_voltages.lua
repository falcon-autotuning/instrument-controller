-- Use native InstrumentTarget accessors and the source test ISA.
local source = require("source")

local function Set_Many_Voltages(ctx, setters, setVoltages)
  assert(#setVoltages == #setters, "one voltage is required per setter")
  for i, setter in ipairs(setters) do
    source:setVoltage(setter:get_instrument_name(), setter:get_channel_group(),
                      setter:get_channel(), setVoltages[i])
  end
end

return { main = Set_Many_Voltages }
