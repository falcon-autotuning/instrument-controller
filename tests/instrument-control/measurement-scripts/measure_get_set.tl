local record InstrumentTarget
  get_instrument_name: function(self: InstrumentTarget): string
  get_channel_group: function(self: InstrumentTarget): string
  get_channel: function(self: InstrumentTarget): integer
end

-- Set each source voltage, then read one scalar value from each meter.
-- Argument order matches the hub's encodeGetSet manifest. Voltages are in V.
local source = require("source")
local multimeter = require("multimeter")

local function Measure_Get_Set(ctx: any, setVoltages: {number}, getters: {InstrumentTarget}, setters: {InstrumentTarget})
  assert(#setVoltages == #setters, "one voltage is required per setter")
  for i, setter in ipairs(setters) do
    source:setVoltage(setter:get_instrument_name(), setter:get_channel_group(),
                      setter:get_channel(), setVoltages[i])
  end
  for _, getter in ipairs(getters) do
    -- GET_DATAPOINT returns a scalar. ISS records the context:call result.
    multimeter:getDatapoint(getter:get_instrument_name(), getter:get_channel_group(),
                            getter:get_channel())
  end
end

return { main = Measure_Get_Set }
