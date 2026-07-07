-- Simple toy measurement test
context:log("Starting toy measurement test")

local resp = context:call("MockInstrument1.GET_DOUBLE")
local val = resp:value()
context:log(string.format("GET_DOUBLE returned: %s", tostring(val)))

context:log("Toy test PASSED")
