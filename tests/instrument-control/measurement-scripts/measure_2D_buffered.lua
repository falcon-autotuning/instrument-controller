-- Scalar raster fallback: sweep X (fast) inside each Y (slow) step.
-- Parameter names and order match the hub's encode2DBuffered manifest.
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

local function Measure2DSweep(ctx, bufferedXSetters, bufferedGetters, bufferedYSetters,
                             numXSteps, setYVoltageDomains, setXVoltageDomains, numYSteps)
  assert(#setXVoltageDomains == #bufferedXSetters, "one X domain is required per setter")
  assert(#setYVoltageDomains == #bufferedYSetters, "one Y domain is required per setter")
  local xSweep, ySweep = sweepMath.buildRasterSweep(setXVoltageDomains, numXSteps,
                                                   setYVoltageDomains, numYSteps)
  for _, yVoltages in ipairs(ySweep) do
    applyVector(bufferedYSetters, yVoltages)
    for _, xVoltages in ipairs(xSweep) do
      applyVector(bufferedXSetters, xVoltages)
      readGetters(bufferedGetters)
    end
  end
end

return { main = Measure2DSweep }
