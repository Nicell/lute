local json = require("@batteries/json")

local start = os.clock()
local i = 0

local data = {
	hello = "world",
	arr = { "foo", 2, true, false, 50000 },
	struct = {
		hi = 5,
		meow = true,
		mrrp = "meow",
		psi = "ψψψψ",
	},
}

local jsonserialize = json.serialize

while os.clock() - start < 1 do
	i += 1
	jsonserialize(data)
end
print(i)

print(jsonserialize(data))

-- local deserialized = json.deserialize([[{
--     "hello": "world"
-- }]])

-- print(deserialized.hello)
