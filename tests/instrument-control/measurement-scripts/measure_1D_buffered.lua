-- Software-stepped 1D sweep: one MEASURE_STREAM buffer per step/getter.
-- Domain arrays are positional, in setter order, and contain native domains.
local sweepMath = require("sweep_math")

local function applyVector(ctx, setters, voltages)
	assert(#setters == #voltages, "one voltage is required per setter")
	for i, setter in ipairs(setters) do
		local command = instrument_call_stack.new({
			instrument = setter:get_instrument_name(),
			group = setter:get_channel_group(),
			channel = setter:get_channel(),
			command = "SET_VOLTAGE",
		})
		ctx:call(command, voltages[i])
	end
end

local function readGetters(ctx, getters)
	for _, getter in ipairs(getters) do
		local command = instrument_call_stack.new({
			instrument = getter:get_instrument_name(),
			group = getter:get_channel_group(),
			channel = getter:get_channel(),
			command = "MEASURE_STREAM",
		})
		ctx:call(command)
	end
end

local function Measure1DBuffered(ctx, setVoltageDomains, getters, numSteps, setters)
	assert(#setVoltageDomains == #setters, "one domain is required per setter")
	local sweep = sweepMath.build1DSweep(setVoltageDomains, numSteps)
	for _, voltages in ipairs(sweep) do
		applyVector(ctx, setters, voltages)
		readGetters(ctx, getters)
	end
end

return { main = Measure1DBuffered }
