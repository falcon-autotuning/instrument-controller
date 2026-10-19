-- Set the meter sample rate in Hz. The mock ISA expects an integer value.
-- The hub sends this target as "setter"; older Teal called it "getter".
local function Set_Sample_Rate(ctx, setter, sampleRate)
	local rate = math.tointeger(sampleRate)
	assert(rate and rate > 0, "sampleRate must be a positive integer in Hz")
	local command = instrument_call_stack.new({
		instrument = setter:get_instrument_name(),
		group = setter:get_channel_group(),
		channel = setter:get_channel(),
		command = "SET_SAMPLE_RATE",
	})
	return ctx:call(command, rate)
end

return { main = Set_Sample_Rate }
