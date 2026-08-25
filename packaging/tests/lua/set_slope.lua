











local function Set_Slope(
   ctx,
   setter,
   slope)

   ctx:log("Acknowledged slope update for " .. setter.id .. ":" .. tostring(setter.channel))
   return ""
end

return { main = Set_Slope }
