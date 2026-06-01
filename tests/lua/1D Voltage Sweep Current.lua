local _tl_compat; if (tonumber((_VERSION or ''):match('[%d.]*$')) or 0) < 5.3 then local p, m = pcall(require, 'compat53.module'); if p then _tl_compat = m end end; local ipairs = _tl_compat and _tl_compat.ipairs or ipairs



















local function VoltageSweepCurrent(
   ctx,
   getters,
   sweepVoltages,
   setters)

   local numPoints = #sweepVoltages
   local sampleRate = 1000
   local endVoltage = sweepVoltages[numPoints] or 0.0


   ctx:parallel(function()
      for _, setter in ipairs(setters) do
         Mock1Source1:setVoltage(setter.id, setter.channel, endVoltage)
      end
      for _, getter in ipairs(getters) do
         Mock5Meter1:setSampleRate(getter.id, getter.channel, sampleRate)
         Mock5Meter1:setBins(getter.id, getter.channel, numPoints)
      end
   end)


   ctx:parallel(function()
      for _, getter in ipairs(getters) do
         Mock5Meter1:measureStream(getter.id, getter.channel)
      end
   end)

   return ""
end
return { main = VoltageSweepCurrent }
