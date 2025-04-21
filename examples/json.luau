local json = require("@batteries/json")

local serialize = json.serialize({
	hello = "world",
	arr = { "foo", 2, true, false, 50000 },
	struct = {
		hi = 5,
		meow = true,
		mrrp = "meow",
	},
}, true)

print(serialize)

-- local deserialized = json.deserialize([[{
--     "hello": "world"
-- }]])

-- print(deserialized.hello)
