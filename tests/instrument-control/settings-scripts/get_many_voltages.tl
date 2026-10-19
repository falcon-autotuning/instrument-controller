local record InstrumentTarget
  get_instrument_name: function(self: InstrumentTarget): string
  get_channel_group: function(self: InstrumentTarget): string
  get_channel: function(self: InstrumentTarget): integer
end

-- Use native InstrumentTarget accessors and the source test ISA.
local source = require("source")

local function Get_Many_Voltages(ctx: any, getters: {InstrumentTarget})
  for _, getter in ipairs(getters) do
    source:getVoltage(getter:get_instrument_name(), getter:get_channel_group(),
                      getter:get_channel())
  end
end

return { main = Get_Many_Voltages }
