-- Sweep helpers; use require("sweep_math") to avoid Lua's built-in math module.
local function linspace(vmin, vmax, nSteps)
	assert(nSteps >= 1 and nSteps % 1 == 0, "step count must be a positive integer")
	local values = {}
	for i = 1, nSteps do
		local fraction = nSteps == 1 and 0 or (i - 1) / (nSteps - 1)
		values[i] = vmin + fraction * (vmax - vmin)
	end
	return values
end

local function build1DSweep(domains, nSteps)
	local fractions = linspace(0, 1, nSteps)
	local sweep = {}
	for i, fraction in ipairs(fractions) do
		local voltages = {}
		for k, domain in ipairs(domains) do
			voltages[k] = domain:get_min() + fraction * (domain:get_max() - domain:get_min())
		end
		sweep[i] = voltages
	end
	return sweep
end

local function buildRasterSweep(xDomains, numXSteps, yDomains, numYSteps)
	return build1DSweep(xDomains, numXSteps), build1DSweep(yDomains, numYSteps)
end

return {
	linspace = linspace,
	build1DSweep = build1DSweep,
	buildRasterSweep = buildRasterSweep,
}
