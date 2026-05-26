local _tl_compat; if (tonumber((_VERSION or ''):match('[%d.]*$')) or 0) < 5.3 then local p, m = pcall(require, 'compat53.module'); if p then _tl_compat = m end end; local ipairs = _tl_compat and _tl_compat.ipairs or ipairs
















local function Gaussian1D(
   ctx,
   getters,
   sweepVoltages,
   setters)

   for _, voltage in ipairs(sweepVoltages) do
      for _, setter in ipairs(setters) do
         source:setVoltage(setter.id, setter.channel, voltage)
      end
      for _, getter in ipairs(getters) do
         multimeter:getDatapoint(getter.id, getter.channel)
      end
   end
   return ""
end
return { main = Gaussian1D }
