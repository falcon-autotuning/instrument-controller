











local function Set_Trigger_Leader(
   ctx,
   getter)

   ctx:log("Acknowledged trigger leader selection for " .. getter.id .. ":" .. tostring(getter.channel))
   return ""
end

return { main = Set_Trigger_Leader }
