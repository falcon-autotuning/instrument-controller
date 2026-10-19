-- Read the meter sample rate in Hz; do not echo a hub-supplied value.
local function Get_Sample_Rate(ctx, getter)
	local command = instrument_call_stack.new({
		instrument = getter:get_instrument_name(),
		group = getter:get_channel_group(),
		channel = getter:get_channel(),
		command = "GET_SAMPLE_RATE",
	})
	return ctx:call(command)
end

return { main = Get_Sample_Rate }
