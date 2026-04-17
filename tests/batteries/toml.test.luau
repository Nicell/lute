local test = require("@std/test")
local toml = require("@batteries/toml")

-- ===== Comments / whitespace =====

test.suite("TOML comments and whitespace", function(suite)
	suite:case("comments are skipped", function(assert)
		local r: any = toml.deserialize([[
# top comment
name = "hello" # trailing comment
# another
]])
		assert.eq(r.name, "hello")
	end)

	suite:case("empty input", function(assert)
		local r: any = toml.deserialize("")
		assert.eq(next(r), nil)
	end)

	suite:case("BOM is stripped", function(assert)
		local r: any = toml.deserialize("\xEF\xBB\xBFa = 1\n")
		assert.eq(r.a, 1)
	end)

	suite:case("CRLF line endings", function(assert)
		local r: any = toml.deserialize("a = 1\r\nb = 2\r\n")
		assert.eq(r.a, 1)
		assert.eq(r.b, 2)
	end)
end)

-- ===== Keys =====

test.suite("TOML keys", function(suite)
	suite:case("bare keys", function(assert)
		local r: any = toml.deserialize("key = 1\nbare_key = 2\nbare-key = 3\n1234 = 4\n")
		assert.eq(r.key, 1)
		assert.eq(r.bare_key, 2)
		assert.eq(r["bare-key"], 3)
		assert.eq(r["1234"], 4)
	end)

	suite:case("quoted keys", function(assert)
		local r: any = toml.deserialize([["127.0.0.1" = "value"
"character encoding" = "value"
"ʎǝʞ" = "value"
'key2' = "value"
'quoted "value"' = "value"
]])
		assert.eq(r["127.0.0.1"], "value")
		assert.eq(r["character encoding"], "value")
		assert.eq(r["ʎǝʞ"], "value")
		assert.eq(r.key2, "value")
		assert.eq(r['quoted "value"'], "value")
	end)

	suite:case("empty quoted key", function(assert)
		local r: any = toml.deserialize('"" = "blank"\n')
		assert.eq(r[""], "blank")
	end)

	suite:case("empty bare key is invalid", function(assert)
		assert.throws(function()
			toml.deserialize("= 1\n")
		end)
	end)

	suite:case("dotted keys build nested tables", function(assert)
		local r: any = toml.deserialize([[
name = "Orange"
physical.color = "orange"
physical.shape = "round"
site."google.com" = true
]])
		assert.eq(r.name, "Orange")
		assert.eq(r.physical.color, "orange")
		assert.eq(r.physical.shape, "round")
		assert.eq(r.site["google.com"], true)
	end)

	suite:case("whitespace around dots is allowed", function(assert)
		local r: any = toml.deserialize("fruit . name = \"banana\"\nfruit . color = \"yellow\"\n")
		assert.eq(r.fruit.name, "banana")
		assert.eq(r.fruit.color, "yellow")
	end)

	suite:case("duplicate key is invalid", function(assert)
		assert.throws(function()
			toml.deserialize("name = 1\nname = 2\n")
		end)
	end)

	suite:case("dotted key cannot redefine existing non-table", function(assert)
		assert.throws(function()
			toml.deserialize("a = 1\na.b = 2\n")
		end)
	end)
end)

-- ===== Strings =====

test.suite("TOML strings", function(suite)
	suite:case("basic string escapes", function(assert)
		local r: any = toml.deserialize([[a = "hello\tworld\n!"]] .. "\n")
		assert.eq(r.a, "hello\tworld\n!")
	end)

	suite:case("unicode escape \\u and \\U", function(assert)
		local r: any = toml.deserialize('a = "\\u00E9"\nb = "\\U0001F600"\n')
		assert.eq(r.a, "é")
		-- U+1F600 in UTF-8 = F0 9F 98 80
		assert.eq(r.b, "\xF0\x9F\x98\x80")
	end)

	suite:case("newline in basic string is invalid", function(assert)
		assert.throws(function()
			toml.deserialize('a = "line\nbreak"\n')
		end)
	end)

	suite:case("literal string has no escapes", function(assert)
		local r: any = toml.deserialize([[a = 'C:\Users\nodejs\templates'
b = '\\ServerX\admin$\system32\'
c = 'Tom "Dubs" Preston-Werner'
]])
		assert.eq(r.a, [[C:\Users\nodejs\templates]])
		assert.eq(r.b, [[\\ServerX\admin$\system32\]])
		assert.eq(r.c, 'Tom "Dubs" Preston-Werner')
	end)

	suite:case("multi-line basic string", function(assert)
		local r: any = toml.deserialize([=[a = """
Roses are red
Violets are blue"""
]=])
		assert.eq(r.a, "Roses are red\nViolets are blue")
	end)

	suite:case("multi-line basic string with line-ending backslash", function(assert)
		local r: any = toml.deserialize([=[a = """\
  The quick brown \
    fox jumps over \
    the lazy dog.\
  """
]=])
		assert.eq(r.a, "The quick brown fox jumps over the lazy dog.")
	end)

	suite:case("multi-line basic string allows 1-2 adjacent quotes", function(assert)
		local r: any = toml.deserialize([=[a = """He said "hello" and "goodbye"."""
]=])
		assert.eq(r.a, 'He said "hello" and "goodbye".')
	end)

	suite:case("multi-line basic string ending with quotes", function(assert)
		local r: any = toml.deserialize([=[a = """Here are two quotation marks: "".  Simple enough."""
b = """Ends with one quote: "" """
c = """Ends with two quotes: """""
]=])
		assert.eq(r.a, 'Here are two quotation marks: "".  Simple enough.')
		assert.eq(r.b, 'Ends with one quote: "" ')
		assert.eq(r.c, 'Ends with two quotes: ""')
	end)

	suite:case("multi-line literal string", function(assert)
		local r: any = toml.deserialize("a = '''\nI [dw]on't need \\d{2} escapes.'''\n")
		assert.eq(r.a, "I [dw]on't need \\d{2} escapes.")
	end)

	suite:case("multi-line literal allows 1-2 adjacent apostrophes", function(assert)
		local r: any = toml.deserialize("a = '''That's still pod'''\n")
		assert.eq(r.a, "That's still pod")
	end)
end)

-- ===== Integers =====

test.suite("TOML integers", function(suite)
	suite:case("decimal integer", function(assert)
		local r: any = toml.deserialize("a = 0\nb = +99\nc = -17\nd = 1_000_000\n")
		assert.eq(r.a, 0)
		assert.eq(r.b, 99)
		assert.eq(r.c, -17)
		assert.eq(r.d, 1000000)
	end)

	suite:case("leading zero rejected", function(assert)
		assert.throws(function()
			toml.deserialize("a = 0123\n")
		end)
	end)

	suite:case("hex / oct / bin integers", function(assert)
		local r: any = toml.deserialize("h = 0xDEADBEEF\no = 0o755\nb = 0b11010110\n")
		assert.eq(r.h, 0xDEADBEEF)
		assert.eq(r.o, 493)
		assert.eq(r.b, 214)
	end)

	suite:case("radix integers allow underscores", function(assert)
		local r: any = toml.deserialize("h = 0xDEAD_BEEF\nb = 0b1101_0110\n")
		assert.eq(r.h, 0xDEADBEEF)
		assert.eq(r.b, 214)
	end)

	suite:case("lone underscore rejected", function(assert)
		assert.throws(function()
			toml.deserialize("a = _1\n")
		end)
		assert.throws(function()
			toml.deserialize("a = 1_\n")
		end)
		assert.throws(function()
			toml.deserialize("a = 1__2\n")
		end)
	end)
end)

-- ===== Floats =====

test.suite("TOML floats", function(suite)
	suite:case("fractions and exponents", function(assert)
		local r: any = toml.deserialize([[
a = 1.0
b = 3.1415
c = -0.01
d = 5e+22
e = 1e06
f = -2E-2
g = 6.626e-34
h = 9_224_617.445_991_228
]])
		assert.eq(r.a, 1.0)
		assert.eq(r.b, 3.1415)
		assert.eq(r.c, -0.01)
		assert.eq(r.d, 5e22)
		assert.eq(r.e, 1e6)
		assert.eq(r.f, -0.02)
		assert.eq(r.g, 6.626e-34)
		assert.eq(r.h, 9224617.445991228)
	end)

	suite:case("inf and nan", function(assert)
		local r: any = toml.deserialize([[
p = inf
q = +inf
r = -inf
s = nan
t = +nan
u = -nan
]])
		assert.eq(r.p, math.huge)
		assert.eq(r.q, math.huge)
		assert.eq(r.r, -math.huge)
		assert.neq(r.s, r.s)
		assert.neq(r.t, r.t)
		assert.neq(r.u, r.u)
	end)

	suite:case("float must have fractional or exponent", function(assert)
		assert.throws(function()
			toml.deserialize("a = 3.\n")
		end)
	end)
end)

-- ===== Booleans =====

test.suite("TOML booleans", function(suite)
	suite:case("true and false", function(assert)
		local r: any = toml.deserialize("a = true\nb = false\n")
		assert.eq(r.a, true)
		assert.eq(r.b, false)
	end)

	suite:case("case-sensitive", function(assert)
		assert.throws(function()
			toml.deserialize("a = True\n")
		end)
	end)
end)

-- ===== Datetimes =====

test.suite("TOML datetimes", function(suite)
	suite:case("offset datetime with Z", function(assert)
		local r: any = toml.deserialize("a = 1979-05-27T07:32:00Z\n")
		assert.eq(r.a._type, "datetime")
		assert.eq(r.a.year, 1979)
		assert.eq(r.a.month, 5)
		assert.eq(r.a.day, 27)
		assert.eq(r.a.hour, 7)
		assert.eq(r.a.minute, 32)
		assert.eq(r.a.second, 0)
		assert.eq(r.a.offset, "Z")
	end)

	suite:case("offset datetime with numeric offset", function(assert)
		local r: any = toml.deserialize("a = 1979-05-27T00:32:00-07:00\n")
		assert.eq(r.a._type, "datetime")
		assert.eq(r.a.offset, "-07:00")
	end)

	suite:case("offset datetime with fractional second", function(assert)
		local r: any = toml.deserialize("a = 1979-05-27T00:32:00.999999-07:00\n")
		assert.eq(r.a._type, "datetime")
		assert.eq(r.a.nanosecond, 999999000)
	end)

	suite:case("local datetime", function(assert)
		local r: any = toml.deserialize("a = 1979-05-27T07:32:00\n")
		assert.eq(r.a._type, "datetime-local")
		assert.eq(r.a.hour, 7)
	end)

	suite:case("local date", function(assert)
		local r: any = toml.deserialize("a = 1979-05-27\n")
		assert.eq(r.a._type, "date-local")
		assert.eq(r.a.year, 1979)
		assert.eq(r.a.month, 5)
		assert.eq(r.a.day, 27)
	end)

	suite:case("local time", function(assert)
		local r: any = toml.deserialize("a = 07:32:00\nb = 00:32:00.999999\n")
		assert.eq(r.a._type, "time-local")
		assert.eq(r.a.hour, 7)
		assert.eq(r.b._type, "time-local")
		assert.eq(r.b.nanosecond, 999999000)
	end)

	suite:case("space separator instead of T", function(assert)
		local r: any = toml.deserialize("a = 1979-05-27 07:32:00Z\n")
		assert.eq(r.a._type, "datetime")
		assert.eq(r.a.hour, 7)
	end)
end)

-- ===== Arrays =====

test.suite("TOML arrays", function(suite)
	suite:case("simple arrays", function(assert)
		local r: any = toml.deserialize([==[
integers = [1, 2, 3]
colors = ["red", "yellow", "green"]
nested_ints = [[1, 2], [3, 4, 5]]
string_array = ["all", 'strings', """are the same""", '''type''']
]==])
		assert.eq(r.integers[1], 1)
		assert.eq(r.integers[3], 3)
		assert.eq(r.colors[2], "yellow")
		assert.eq(r.nested_ints[1][2], 2)
		assert.eq(r.nested_ints[2][3], 5)
		assert.eq(r.string_array[4], "type")
	end)

	suite:case("mixed-type arrays allowed in v1.0", function(assert)
		local r: any = toml.deserialize("numbers = [0.1, 0.2, 0.5, 1, 2, 5]\n")
		assert.eq(r.numbers[1], 0.1)
		assert.eq(r.numbers[5], 2)
	end)

	suite:case("multi-line arrays with trailing comma", function(assert)
		local r: any = toml.deserialize([[
arr = [
  1,
  2, # comment
  3,
]
]])
		assert.eq(r.arr[1], 1)
		assert.eq(r.arr[3], 3)
		assert.eq(#r.arr, 3)
	end)

	suite:case("empty array", function(assert)
		local r: any = toml.deserialize("a = []\n")
		assert.eq(#r.a, 0)
	end)
end)

-- ===== Inline tables =====

test.suite("TOML inline tables", function(suite)
	suite:case("basic inline table", function(assert)
		local r: any = toml.deserialize('point = { x = 1, y = 2 }\n')
		assert.eq(r.point.x, 1)
		assert.eq(r.point.y, 2)
	end)

	suite:case("empty inline table", function(assert)
		local r: any = toml.deserialize("empty = {}\n")
		assert.eq(typeof(r.empty), "table")
		assert.eq(next(r.empty), nil)
	end)

	suite:case("inline table with dotted keys", function(assert)
		local r: any = toml.deserialize('point = { x.coord = 1, x.label = "origin" }\n')
		assert.eq(r.point.x.coord, 1)
		assert.eq(r.point.x.label, "origin")
	end)

	suite:case("trailing comma rejected", function(assert)
		assert.throws(function()
			toml.deserialize("t = { a = 1, }\n")
		end)
	end)

	suite:case("newline inside inline table rejected", function(assert)
		assert.throws(function()
			toml.deserialize("t = { a = 1,\n b = 2 }\n")
		end)
	end)

	suite:case("inline table cannot be extended", function(assert)
		assert.throws(function()
			toml.deserialize("t = { a = 1 }\nt.b = 2\n")
		end)
	end)
end)

-- ===== Tables =====

test.suite("TOML table headers", function(suite)
	suite:case("standard table", function(assert)
		local r: any = toml.deserialize("[table]\nkey = \"value\"\n")
		assert.eq(r.table.key, "value")
	end)

	suite:case("dotted table path", function(assert)
		local r: any = toml.deserialize("[dog.\"tater.man\"]\ntype.name = \"pug\"\n")
		assert.eq(r.dog["tater.man"].type.name, "pug")
	end)

	suite:case("duplicate table header rejected", function(assert)
		assert.throws(function()
			toml.deserialize("[a]\nb = 1\n[a]\nc = 2\n")
		end)
	end)

	suite:case("dotted key defines cannot be redefined by table header", function(assert)
		-- After `[fruit]` with `apple.color = "red"`, `[fruit.apple]` is invalid
		assert.throws(function()
			toml.deserialize([[
[fruit]
apple.color = "red"
[fruit.apple]
taste = "sweet"
]])
		end)
	end)

	suite:case("table header can add sub-tables to dotted-key tables", function(assert)
		local r: any = toml.deserialize([[
[fruit]
apple.color = "red"

[fruit.apple.texture]
smooth = true
]])
		assert.eq(r.fruit.apple.color, "red")
		assert.eq(r.fruit.apple.texture.smooth, true)
	end)

	suite:case("implicit parent promoted to explicit", function(assert)
		local r: any = toml.deserialize([[
[a.b.c]
answer = 42

[a]
better = 43
]])
		assert.eq(r.a.b.c.answer, 42)
		assert.eq(r.a.better, 43)
	end)
end)

-- ===== Array of tables =====

test.suite("TOML array of tables", function(suite)
	suite:case("basic array of tables", function(assert)
		local r: any = toml.deserialize([==[
[[products]]
name = "Hammer"
sku = 738594937

[[products]]

[[products]]
name = "Nail"
sku = 284758393
color = "gray"
]==])
		assert.eq(#r.products, 3)
		assert.eq(r.products[1].name, "Hammer")
		assert.eq(next(r.products[2]), nil)
		assert.eq(r.products[3].color, "gray")
	end)

	suite:case("nested array of tables", function(assert)
		local r: any = toml.deserialize([==[
[[fruits]]
name = "apple"

[fruits.physical]
color = "red"
shape = "round"

[[fruits.varieties]]
name = "red delicious"

[[fruits.varieties]]
name = "granny smith"

[[fruits]]
name = "banana"

[[fruits.varieties]]
name = "plantain"
]==])
		assert.eq(#r.fruits, 2)
		assert.eq(r.fruits[1].name, "apple")
		assert.eq(r.fruits[1].physical.color, "red")
		assert.eq(#r.fruits[1].varieties, 2)
		assert.eq(r.fruits[1].varieties[2].name, "granny smith")
		assert.eq(r.fruits[2].name, "banana")
		assert.eq(r.fruits[2].varieties[1].name, "plantain")
	end)

	suite:case("table cannot be redefined as array of tables", function(assert)
		assert.throws(function()
			toml.deserialize([==[
[fruit]
name = "apple"

[[fruit]]
name = "banana"
]==])
		end)
	end)
end)

-- ===== Original tests, preserved =====

test.suite("TOML deserialization", function(suite)
	suite:case("simpleKeyValue", function(assert)
		local result: any = toml.deserialize('name = "hello"\ncount = 42\nenabled = true\n')
		assert.eq(result.name, "hello")
		assert.eq(result.count, 42)
		assert.eq(result.enabled, true)
	end)

	suite:case("nestedTableViaSection", function(assert)
		local result: any = toml.deserialize('[server]\nhost = "localhost"\nport = 8080\n')
		assert.eq(result.server.host, "localhost")
		assert.eq(result.server.port, 8080)
	end)

	suite:case("dottedTablePath", function(assert)
		local result: any = toml.deserialize("[a.b]\nvalue = 1\n")
		assert.eq(result.a.b.value, 1)
	end)

	suite:case("quotedKeyWithDotInTableHeader", function(assert)
		local input = '[images."icon.png"]\nassetId = "123"\n'
		local result: any = toml.deserialize(input)
		assert.neq(result.images, nil)
		assert.neq(result.images["icon.png"], nil)
		assert.eq(result.images["icon.png"].assetId, "123")
	end)

	suite:case("singleQuotedKeyWithDotInTableHeader", function(assert)
		local input = "[images.'icon.png']\nassetId = \"456\"\n"
		local result: any = toml.deserialize(input)
		assert.neq(result.images, nil)
		assert.neq(result.images["icon.png"], nil)
		assert.eq(result.images["icon.png"].assetId, "456")
	end)

	suite:case("mixedDottedAndQuotedPath", function(assert)
		local input = '[a."b.c".d]\nvalue = 1\n'
		local result: any = toml.deserialize(input)
		assert.neq(result.a, nil)
		assert.neq(result.a["b.c"], nil)
		assert.neq(result.a["b.c"].d, nil)
		assert.eq(result.a["b.c"].d.value, 1)
	end)

	suite:case("quotedKeyInKeyValuePair", function(assert)
		local result: any = toml.deserialize('"icon.png" = "value"\n')
		assert.eq(result["icon.png"], "value")
	end)

	suite:case("specialValues", function(assert)
		local result: any = toml.deserialize("a = inf\nb = -inf\nc = nan\n")
		assert.eq(result.a, math.huge)
		assert.eq(result.b, -math.huge)
		assert.neq(result.c, result.c)
	end)
end)

test.suite("TOML serialization", function(suite)
	suite:case("keyWithDotIsQuoted", function(assert)
		local input = { images = { ["icon.png"] = { assetId = "123" } } }
		local output = toml.serialize(input)
		assert.strContains(output, '"icon.png"')
	end)

	suite:case("roundTrip", function(assert)
		local input = '[images."icon.png"]\nassetId = "123"\nhash = "abc"\n'
		local first: any = toml.deserialize(input)
		local serialized = toml.serialize(first)
		local second: any = toml.deserialize(serialized)
		assert.eq(second.images["icon.png"].assetId, "123")
		assert.eq(second.images["icon.png"].hash, "abc")
	end)

	suite:case("serializes all primitives", function(assert)
		local output = toml.serialize({
			s = "hello",
			n = 42,
			f = 3.14,
			t = true,
			inf = math.huge,
			neginf = -math.huge,
		})
		assert.strContains(output, 's = "hello"')
		assert.strContains(output, "n = 42")
		assert.strContains(output, "f = 3.14")
		assert.strContains(output, "t = true")
		assert.strContains(output, "inf = inf")
		assert.strContains(output, "neginf = -inf")
	end)

	suite:case("serializes strings with escapes", function(assert)
		local output = toml.serialize({ s = "line\nbreak\ttab" })
		assert.strContains(output, [[s = "line\nbreak\ttab"]])
	end)

	suite:case("serializes arrays of primitives", function(assert)
		local output = toml.serialize({ xs = { 1, 2, 3 } })
		assert.strContains(output, "xs = [1, 2, 3]")
	end)

	suite:case("serializes arrays of tables", function(assert)
		local input = { users = { { name = "Alice" }, { name = "Bob" } } }
		local output = toml.serialize(input)
		assert.strContains(output, "[[users]]", 2)
		local parsed: any = toml.deserialize(output)
		assert.eq(parsed.users[1].name, "Alice")
		assert.eq(parsed.users[2].name, "Bob")
	end)

	suite:case("serializes datetimes via helpers", function(assert)
		local input = {
			t = toml.datetime({ year = 1979, month = 5, day = 27, hour = 7, minute = 32, second = 0, offset = "Z" }),
			d = toml.date({ year = 2024, month = 1, day = 1 }),
		}
		local output = toml.serialize(input)
		assert.strContains(output, "1979-05-27T07:32:00Z")
		assert.strContains(output, "2024-01-01")
	end)

	suite:case("roundtrips datetimes", function(assert)
		local input = "t = 1979-05-27T07:32:00Z\nd = 2024-01-01\n"
		local first: any = toml.deserialize(input)
		local out = toml.serialize(first)
		local second: any = toml.deserialize(out)
		assert.eq(second.t._type, "datetime")
		assert.eq(second.t.offset, "Z")
		assert.eq(second.d._type, "date-local")
		assert.eq(second.d.year, 2024)
	end)
end)
