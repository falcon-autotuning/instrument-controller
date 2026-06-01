local _tl_compat; if (tonumber((_VERSION or ''):match('[%d.]*$')) or 0) < 5.3 then local p, m = pcall(require, 'compat53.module'); if p then _tl_compat = m end end; local ipairs = _tl_compat and _tl_compat.ipairs or ipairs


















local function VoltageSweepCurrent2D(
   ctx,
   getters,
   fastSweepVoltages,
   slowSweepVoltages,
   fastSetter,
   slowSetter)

   local numFastPoints = #fastSweepVoltages
   local fastEndVoltage = fastSweepVoltages[numFastPoints] or 0.0

   for _, slowV in ipairs(slowSweepVoltages) do


      ctx:parallel(function()
         Mock1Source1:setVoltage(slowSetter.id, slowSetter.channel, slowV)
         Mock1Source1:setVoltage(fastSetter.id, fastSetter.channel, fastEndVoltage)
         for _, getter in ipairs(getters) do
            Mock5Meter1:setSampleRate(getter.id, getter.channel, 1000)
            Mock5Meter1:setBins(getter.id, getter.channel, numFastPoints)
         end
      end)



      ctx:parallel(function()
         for _, getter in ipairs(getters) do
            Mock5Meter1:measureStream(getter.id, getter.channel)
         end
      end)
   end

   return ""
end

return { main = VoltageSweepCurrent2D }
