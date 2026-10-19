-- Scalar fallback for a 1D sweep: one recorded GET_DATAPOINT per step/getter.
-- Domain arrays are positional, in setter order, and contain native domains.
local source = require("source")
local multimeter = require("multimeter")
local sweepMath = require("sweep_math")

local function applyVector(setters, voltages)
  assert(#setters == #voltages, "one voltage is required per setter")
  for i, setter in ipairs(setters) do
    source:setVoltage(setter:get_instrument_name(), setter:get_channel_group(),
                      setter:get_channel(), voltages[i])
  end
end

local function readGetters(getters)
  for _, getter in ipairs(getters) do
    multimeter:getDatapoint(getter:get_instrument_name(), getter:get_channel_group(),
                            getter:get_channel())
  end
end

local function Measure1DBuffered(ctx, setVoltageDomains, getters, numSteps, setters)
  assert(#setVoltageDomains == #setters, "one domain is required per setter")
  local sweep = sweepMath.build1DSweep(sweepMath.limitsFromDomains(setVoltageDomains), numSteps)
  for _, voltages in ipairs(sweep) do
    applyVector(setters, voltages)
    readGetters(getters)
  end
end

return { main = Measure1DBuffered }
