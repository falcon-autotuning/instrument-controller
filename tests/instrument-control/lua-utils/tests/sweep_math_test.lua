local sweep = require("sweep_math")

local function assertEqual(a, b)
	assert(a == b, string.format("expected %s, got %s", b, a))
end

local function assertArrayEqual(a, b)
	assert(#a == #b, "array lengths differ")
	for i = 1, #a do
		assertEqual(a[i], b[i])
	end
end

-- linspace(0,10,1)
do
	local values = sweep.linspace(0, 10, 1)
	assertArrayEqual(values, { 0 })
end

-- linspace(0,10,2)
do
	local values = sweep.linspace(0, 10, 2)
	assertArrayEqual(values, { 0, 10 })
end

-- linspace(0,10,5)
do
	local values = sweep.linspace(0, 10, 5)
	assertArrayEqual(values, { 0, 2.5, 5, 7.5, 10 })
end

print("all tests passed")
