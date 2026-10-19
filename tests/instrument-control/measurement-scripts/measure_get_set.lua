-- Set each source voltage, then read one scalar value from each meter.
-- Argument order matches the hub's encodeGetSet manifest. Voltages are in V.
local function Measure_Get_Set(ctx, setVoltages, getters, setters)
  assert(#setVoltages == #setters, "one voltage is required per setter")
  for i, setter in ipairs(setters) do
    local command = instrument_call_stack.new({
      instrument = setter:get_instrument_name(),
      group = setter:get_channel_group(),
      channel = setter:get_channel(),
      command = "SET_VOLTAGE",
    })
    ctx:call(command, setVoltages[i])
  end
  for _, getter in ipairs(getters) do
    -- GET_DATAPOINT returns a scalar. ISS records the context:call result.
    local command = instrument_call_stack.new({
      instrument = getter:get_instrument_name(),
      group = getter:get_channel_group(),
      channel = getter:get_channel(),
      command = "GET_DATAPOINT",
    })
    ctx:call(command)
  end
end

return { main = Measure_Get_Set }
