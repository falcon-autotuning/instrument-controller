-- Use native InstrumentTarget accessors and the source test ISA.
local source = require("source")

local function Get_Many_Voltages(ctx, getters)
  for _, getter in ipairs(getters) do
    source:getVoltage(getter:get_instrument_name(), getter:get_channel_group(),
                      getter:get_channel())
  end
end

return { main = Get_Many_Voltages }
