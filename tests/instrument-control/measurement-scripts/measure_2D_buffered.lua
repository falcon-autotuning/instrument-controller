-- Software-stepped raster: sweep X inside each Y step and record buffers.
-- Parameter names and order match the hub's encode2DBuffered manifest.
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

local function Measure2DSweep(
	ctx,
	bufferedXSetters,
	bufferedGetters,
	bufferedYSetters,
	numXSteps,
	setYVoltageDomains,
	setXVoltageDomains,
	numYSteps
)
	assert(#setXVoltageDomains == #bufferedXSetters, "one X domain is required per setter")
	assert(#setYVoltageDomains == #bufferedYSetters, "one Y domain is required per setter")
	local xSweep, ySweep = sweepMath.buildRasterSweep(setXVoltageDomains, numXSteps, setYVoltageDomains, numYSteps)
	for _, yVoltages in ipairs(ySweep) do
		applyVector(ctx, bufferedYSetters, yVoltages)
		for _, xVoltages in ipairs(xSweep) do
			applyVector(ctx, bufferedXSetters, xVoltages)
			readGetters(ctx, bufferedGetters)
		end
	end
end

return { main = Measure2DSweep }
