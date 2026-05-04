local _tl_compat; if (tonumber((_VERSION or ''):match('[%d.]*$')) or 0) < 5.3 then local p, m = pcall(require, 'compat53.module'); if p then _tl_compat = m end end; local math = _tl_compat and _tl_compat.math or math


local function _log10(x)
   return math.log(x) / math.log(10)
end

local function _round_to_sig(x, n)
   if x == 0 then return 0 end
   local d = math.floor(_log10(math.abs(x)))
   local scale = 10 ^ (d - n + 1)
   return math.floor((x / scale) + 0.5) * scale
end

local function _round_to_multiple(x, step)
   if step == 0 then return x end
   return math.floor((x / step) + 0.5) * step
end

local function _int_keep_significant(v, n)
   if v == 0 then return 0 end
   local neg = v < 0
   local a = math.abs(v)
   local digits = math.floor(_log10(a)) + 1
   if digits <= n then return v end
   local factor = 10 ^ (digits - n)
   local res = math.floor((a / factor) + 0.5) * factor
   if neg then res = -res end
   return res
end

local Mock5Meter1 = {}






function Mock5Meter1:setSampleRate(id, channel, sample_rate)
   local _old_channel = channel
   if channel < 1.000000 then
      channel = 1.000000
      context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
   end
   if channel > 8.000000 then
      channel = 8.000000
      context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
   end
   channel = math.floor(channel)
   return context:call(id .. ':' .. tostring(channel) .. '.SET_SAMPLE_RATE', sample_rate)
end






function Mock5Meter1:setBins(id, channel, bins)
   local _old_channel = channel
   if channel < 1.000000 then
      channel = 1.000000
      context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
   end
   if channel > 8.000000 then
      channel = 8.000000
      context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
   end
   channel = math.floor(channel)
   return context:call(id .. ':' .. tostring(channel) .. '.SET_BINS', bins)
end





function Mock5Meter1:measureStream(id, channel)
   local _old_channel = channel
   if channel < 1.000000 then
      channel = 1.000000
      context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
   end
   if channel > 8.000000 then
      channel = 8.000000
      context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
   end
   channel = math.floor(channel)
   return context:call(id .. ':' .. tostring(channel) .. '.MEASURE_STREAM')
end





function Mock5Meter1:getDatapoint(id, channel)
   local _old_channel = channel
   if channel < 1.000000 then
      channel = 1.000000
      context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
   end
   if channel > 8.000000 then
      channel = 8.000000
      context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
   end
   channel = math.floor(channel)
   return context:call(id .. ':' .. tostring(channel) .. '.GET_DATAPOINT')
end




function Mock5Meter1:reset(id)
   return context:call(id .. '.RESET')
end

return Mock5Meter1
