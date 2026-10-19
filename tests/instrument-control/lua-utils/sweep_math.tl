local record InstrumentDomain
  get_min: function(self: InstrumentDomain): number
  get_max: function(self: InstrumentDomain): number
end

-- Sweep helpers; use require("sweep_math") to avoid Lua's built-in math module.
local function linspace(vmin: number, vmax: number, nSteps: integer): {number}
  assert(nSteps >= 1 and nSteps % 1 == 0, "step count must be a positive integer")
  local values: {number} = {}
  for i = 1, nSteps do
    local fraction = nSteps == 1 and 0 or (i - 1) / (nSteps - 1)
    values[i] = vmin + fraction * (vmax - vmin)
  end
  return values
end

local function build1DSweep(limits: {{number}}, nSteps: integer): {{number}}
  local fractions = linspace(0, 1, nSteps)
  local sweep: {{number}} = {}
  for i, fraction in ipairs(fractions) do
    local voltages: {number} = {}
    for k, limit in ipairs(limits) do
      voltages[k] = limit[1] + fraction * (limit[2] - limit[1])
    end
    sweep[i] = voltages
  end
  return sweep
end

local function limitsFromDomains(domains: {InstrumentDomain}): {{number}}
  local limits: {{number}} = {}
  for i, domain in ipairs(domains) do
    limits[i] = {domain:get_min(), domain:get_max()}
  end
  return limits
end

local function buildRasterSweep(xDomains: {InstrumentDomain}, numXSteps: integer, yDomains: {InstrumentDomain}, numYSteps: integer): {{number}}, {{number}}
  return build1DSweep(limitsFromDomains(xDomains), numXSteps),
         build1DSweep(limitsFromDomains(yDomains), numYSteps)
end

return {
  linspace = linspace,
  build1DSweep = build1DSweep,
  limitsFromDomains = limitsFromDomains,
  buildRasterSweep = buildRasterSweep,
}
