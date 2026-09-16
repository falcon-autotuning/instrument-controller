



Mock1Source1 = {}






function Mock1Source1:setVoltage(id, channel, voltage)
   local cs = instrument_call_stack.new({
      instrument = id,
      command = "SET_VOLTAGE",
      channel = channel,
   })
   return context:call(cs, voltage)
end





function Mock1Source1:getVoltage(id, channel)
   local cs = instrument_call_stack.new({
      instrument = id,
      command = "GET_VOLTAGE",
      channel = channel,
   })
   return context:call(cs)
end




function Mock1Source1:reset(id)
   local cs = instrument_call_stack.new({
      instrument = id,
      command = "RESET",
   })
   return context:call(cs)
end

return Mock1Source1
