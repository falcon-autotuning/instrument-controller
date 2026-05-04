









local function Set_Voltage(
   ctx,
   setVoltage,
   setter)

   if setter.id ~= "Source1" then
      ctx:error("Invalid setter id: " .. setter.id)
      return nil
   end
   Mock1Source1:setVoltage(setter.channel, setVoltage)
   return nil
end
return { main = Set_Voltage }
