--!strict
-- @std/json
-- JSON serialization and deserialization library

local json = {
	--- A sentinel value representing JSON `null`. Not actually `nil` — use this when you need to serialize a null field.
	null = newproxy() :: nil,
}

--- A JSON object: a table with string keys.
export type Object = { [string]: Value }
--- A JSON array: a table with integer keys.
export type Array = { [number]: Value }
--- Any JSON value: `null`, a number, string, boolean, array, or object.
export type Value = nil | number | string | boolean | Array | Object

-- a unique key to identify an object vs a table
local object_key = newproxy()

-- serialization

-- Map of bytes that must be escaped inside a JSON string. Built once at module
-- load; the slow-path `string.gsub` uses this as the replacement table.
local STRING_ESCAPES: { [string]: string } = {
	["\\"] = "\\\\",
	['"'] = '\\"',
	["\b"] = "\\b",
	["\f"] = "\\f",
	["\n"] = "\\n",
	["\r"] = "\\r",
	["\t"] = "\\t",
}
for i = 0, 31 do
	local c = string.char(i)
	if not STRING_ESCAPES[c] then
		STRING_ESCAPES[c] = string.format("\\u%04x", i)
	end
end

-- Pattern matching any byte that requires escaping.
local STRING_NEEDS_ESCAPE = '[%z\1-\31"\\]'

-- Cache of pretty-print indent strings per depth.
local INDENT_CACHE: { [number]: string } = { [0] = "" }
local function indentFor(depth: number): string
	local cached = INDENT_CACHE[depth]
	if cached then
		return cached
	end
	local s = string.rep("    ", depth)
	INDENT_CACHE[depth] = s
	return s
end

local null = json.null

-- Hot-path: serialize without indentation. The compact serializer threads
-- `(out, n)` through call args/returns to avoid touching a state table on
-- every primitive.
local serializeCompact: ({ string }, number, (Array | Object | boolean | number | string)?) -> number

local function serializeStringCompact(out: { string }, n: number, str: string): number
	n += 1
	if string.find(str, STRING_NEEDS_ESCAPE) then
		out[n] = '"' .. string.gsub(str, STRING_NEEDS_ESCAPE, STRING_ESCAPES) .. '"'
	else
		out[n] = '"' .. str .. '"'
	end
	return n
end

local function serializeArrayCompact(out: { string }, n: number, array: Array): number
	local len = #array
	if len == 0 then
		n += 1
		out[n] = "[]"
		return n
	end

	n += 1
	out[n] = "["
	n = serializeCompact(out, n, array[1])

	for i = 2, len do
		n += 1
		out[n] = ","
		n = serializeCompact(out, n, array[i])
	end

	n += 1
	out[n] = "]"
	return n
end

local function serializeTableCompact(out: { string }, n: number, object: Object): number
	n += 1
	out[n] = "{"

	local first = true
	for key, value in object do
		if key == object_key then
			continue
		end

		if first then
			first = false
		else
			n += 1
			out[n] = ","
		end

		n = serializeStringCompact(out, n, key)
		n += 1
		out[n] = ":"

		-- Inlined primitive dispatch: real-world objects average many small
		-- leaf values per record, so saving the `serializeCompact` call here
		-- is worth ~10% on object-heavy payloads.
		local valueType = type(value)
		if valueType == "string" then
			n = serializeStringCompact(out, n, value :: string)
		elseif valueType == "number" then
			n += 1
			out[n] = tostring(value)
		elseif valueType == "boolean" then
			n += 1
			out[n] = if value then "true" else "false"
		else
			n = serializeCompact(out, n, value)
		end
	end

	n += 1
	out[n] = "}"
	return n
end

serializeCompact = function(out: { string }, n: number, value: Value): number
	local valueType = type(value)

	if valueType == "string" then
		return serializeStringCompact(out, n, value :: string)
	elseif valueType == "number" then
		n += 1
		out[n] = tostring(value)
		return n
	elseif valueType == "boolean" then
		n += 1
		out[n] = if value then "true" else "false"
		return n
	elseif value == null then
		n += 1
		out[n] = "null"
		return n
	elseif valueType == "table" then
		local t = value :: Array
		if #t == 0 and next(t :: any) ~= nil then
			return serializeTableCompact(out, n, value :: Object)
		else
			return serializeArrayCompact(out, n, t)
		end
	else
		error("Unknown value", 2)
	end
end

-- Pretty-print path. Slower; depth is threaded explicitly.
local serializePretty: ({ string }, number, (Array | Object | boolean | number | string)?, number) -> number

local function serializeArrayPretty(out: { string }, n: number, array: Array, depth: number): number
	local len = #array
	if len == 0 then
		n += 1
		out[n] = "[]"
		return n
	end

	local inner = indentFor(depth + 1)
	local outer = indentFor(depth)

	n += 1
	out[n] = "[\n"
	n += 1
	out[n] = inner
	n = serializePretty(out, n, array[1], depth + 1)

	for i = 2, len do
		n += 1
		out[n] = ",\n"
		n += 1
		out[n] = inner
		n = serializePretty(out, n, array[i], depth + 1)
	end

	n += 1
	out[n] = "\n"
	n += 1
	out[n] = outer
	n += 1
	out[n] = "]"
	return n
end

local function serializeTablePretty(out: { string }, n: number, object: Object, depth: number): number
	local inner = indentFor(depth + 1)
	local outer = indentFor(depth)

	n += 1
	out[n] = "{"

	local first = true
	for key, value in object do
		if key == object_key then
			continue
		end

		if first then
			first = false
			n += 1
			out[n] = "\n"
		else
			n += 1
			out[n] = ",\n"
		end
		n += 1
		out[n] = inner

		n = serializeStringCompact(out, n, key)

		n += 1
		out[n] = ": "

		n = serializePretty(out, n, value, depth + 1)
	end

	if first then
		n += 1
		out[n] = "}"
	else
		n += 1
		out[n] = "\n"
		n += 1
		out[n] = outer
		n += 1
		out[n] = "}"
	end
	return n
end

serializePretty = function(out: { string }, n: number, value: Value, depth: number): number
	local valueType = type(value)

	if valueType == "string" then
		return serializeStringCompact(out, n, value :: string)
	elseif valueType == "number" then
		n += 1
		out[n] = tostring(value)
		return n
	elseif valueType == "boolean" then
		n += 1
		out[n] = if value then "true" else "false"
		return n
	elseif value == null then
		n += 1
		out[n] = "null"
		return n
	elseif valueType == "table" then
		local t = value :: Array
		if #t == 0 and next(t :: any) ~= nil then
			return serializeTablePretty(out, n, value :: Object, depth)
		else
			return serializeArrayPretty(out, n, t, depth)
		end
	else
		error("Unknown value", 2)
	end
end

-- deserialization
--
-- Threaded-state design: each parser takes `(src, cursor)` and returns
-- `(newCursor, value)`. Cursor is 1-indexed (Lua string convention) and points
-- at the *next* byte to consume.

local function parseError(cursor: number, msg: string): never
	return error(`JSON error - {msg} around {cursor}`)
end

-- skipWs: returns `(newCursor, firstByte)` where firstByte is the byte at the
-- new cursor position (or nil at EOF). Returning the byte saves callers an
-- extra `string.byte` since they always inspect the next byte.
-- `string.find` with a character class jumps over runs of ws in one C call.
local function skipWs(src: string, cursor: number): (number, number?)
	-- Common case in compact JSON: cursor is already on a non-ws byte.
	local b = string.byte(src, cursor)
	if b == nil then
		return cursor, nil
	end
	if b ~= 0x20 and b ~= 0x09 and b ~= 0x0A and b ~= 0x0D then
		return cursor, b
	end
	cursor = (string.find(src, "[^ \t\r\n]", cursor)) or (#src + 1)
	return cursor, string.byte(src, cursor)
end

-- Escape replacement table for the slow path of string parsing.
local UNESCAPE: { [string]: string } = {
	['"'] = '"',
	["\\"] = "\\",
	["/"] = "/",
	b = "\b",
	f = "\f",
	n = "\n",
	r = "\r",
	t = "\t",
}

local function decodeUnicodeEscape(hex: string, cursor: number): string
	local cp = tonumber(hex, 16) :: number
	if cp >= 0xD800 and cp <= 0xDBFF then
		parseError(cursor, "Incomplete surrogate pair")
	end
	if cp >= 0xDC00 and cp <= 0xDFFF then
		parseError(cursor, "Lone low surrogate")
	end
	return utf8.char(cp)
end

local function decodeSurrogatePair(highHex: string, lowHex: string, cursor: number): string
	local high = tonumber(highHex, 16) :: number
	local low = tonumber(lowHex, 16) :: number
	if low < 0xDC00 or low > 0xDFFF then
		parseError(cursor, "Invalid unicode surrogate pair")
	end
	local codepoint = 0x10000 + ((high - 0xD800) * 0x400) + (low - 0xDC00)
	return utf8.char(codepoint)
end

-- parseString: cursor is at the opening `"`. Returns (newCursor, decoded).
local function parseString(src: string, cursor: number): (number, string)
	local startPos = cursor + 1

	-- Fast path: scan for the first byte that is either a closing quote, a
	-- backslash (escape), or a control character. If we find a quote first
	-- and never see a backslash, the string contents are exactly the bytes
	-- between the quotes — one `string.sub`, no allocation churn.
	local p = string.find(src, '[\\"%z\1-\31]', startPos)
	if not p then
		parseError(cursor, "Unterminated string")
	end
	local b = string.byte(src, p)
	if b == 0x22 then
		return p + 1, string.sub(src, startPos, p - 1)
	end
	if b ~= 0x5C then
		parseError(p, "Unescaped control character in string")
	end

	-- Slow path: there is at least one backslash. Walk to the closing quote,
	-- skipping escape sequences so we don't match `\"` as the terminator.
	local len = #src
	while p <= len do
		local c = string.byte(src, p)
		if c == 0x5C then
			p += 2
		elseif c == 0x22 then
			break
		elseif c < 0x20 then
			parseError(p, "Unescaped control character in string")
		else
			p += 1
		end
	end
	if p > len then
		parseError(cursor, "Unterminated string")
	end

	local raw = string.sub(src, startPos, p - 1)

	-- Resolve escapes in five gsub passes (vs. ten in the old code). We use a
	-- 0x01 sentinel to "freeze" `\\` sequences before the other passes run,
	-- so that a `\\u0000` source isn't mis-parsed as backslash + `\u0000`.
	-- The fast-path scan above guarantees the input contains no 0x01 bytes.
	--   1. `\\` → 0x01            (freeze raw backslashes)
	--   2. `\uHIGH\uLOW`           → utf8 surrogate pair
	--   3. `\uXXXX`                → utf8 single code point
	--   4. `\X` for X in `"/bfnrt` → corresponding char
	--   5. 0x01 → `\`              (thaw)
	raw = string.gsub(raw, "\\\\", "\1")
	raw = string.gsub(raw, "\\u([dD][89aAbB]%x%x)\\u([dD][c-fC-F]%x%x)", function(h, l)
		return decodeSurrogatePair(h, l, cursor)
	end :: any)
	raw = string.gsub(raw, "\\u(%x%x%x%x)", function(code)
		return decodeUnicodeEscape(code, cursor)
	end :: any)
	raw = string.gsub(raw, "\\(.)", function(esc)
		local v = UNESCAPE[esc]
		if not v then
			parseError(cursor, "Invalid escape sequence")
		end
		return v
	end :: any)
	raw = string.gsub(raw, "\1", "\\")

	return p + 1, raw
end

-- skipDigits: advance cursor past any run of [0-9]. Uses `string.find` for
-- one C call per run instead of an interpreted byte-by-byte loop.
local function skipDigits(src: string, cursor: number): number
	local p = string.find(src, "[^0-9]", cursor)
	return p or (#src + 1)
end

-- parseNumber: walk + validate the JSON number grammar in a single pass.
-- We can't just use tonumber because Lua accepts forms JSON forbids ("1.",
-- ".5", "0x1f", "1.e3", "inf", etc).
local function parseNumber(src: string, cursor: number): (number, number)
	local start = cursor

	-- optional leading minus
	local b = string.byte(src, cursor)
	if b == 0x2D then
		cursor += 1
		b = string.byte(src, cursor)
	end

	-- integer part: "0" or [1-9][0-9]*
	if b == 0x30 then
		cursor += 1
		b = string.byte(src, cursor)
	elseif b and b >= 0x31 and b <= 0x39 then
		cursor = skipDigits(src, cursor + 1)
		b = string.byte(src, cursor)
	else
		parseError(start, "Malformed number (bad integer part)")
	end

	-- optional fraction: "." [0-9]+
	if b == 0x2E then
		local afterDot = cursor + 1
		cursor = skipDigits(src, afterDot)
		if cursor == afterDot then
			parseError(start, "Malformed number (no fractional digits)")
		end
		b = string.byte(src, cursor)
	end

	-- optional exponent: [eE] [+-]? [0-9]+
	if b == 0x65 or b == 0x45 then
		cursor += 1
		b = string.byte(src, cursor)
		if b == 0x2B or b == 0x2D then
			cursor += 1
		end
		local expDigitsStart = cursor
		cursor = skipDigits(src, cursor)
		if cursor == expDigitsStart then
			parseError(start, "Malformed number (no exponent digits)")
		end
	end

	local n = tonumber(string.sub(src, start, cursor - 1))
	if not n then
		parseError(start, "Malformed number")
	end
	return cursor, n :: number
end

-- Forward declarations for the mutually-recursive parsers.
local parseArray: (string, number) -> (number, Array)
local parseObject: (string, number) -> (number, Object)

-- parseValueAt: given that we've already peeked at the next byte `b`, parse a
-- value starting there. Splitting this out from parseValue lets parseObject
-- and parseArray reuse the byte they already read for the comma/bracket check.
local function parseValueAt(src: string, cursor: number, b: number?): (number, Value)
	if b == 0x22 then -- '"'
		return parseString(src, cursor)
	elseif b == 0x7B then -- '{'
		return parseObject(src, cursor)
	elseif b == 0x5B then -- '['
		return parseArray(src, cursor)
	elseif b == 0x74 then -- 't' — match "true" via byte compare to avoid the
		-- substring allocation that `string.sub` would do.
		local b1, b2, b3 = string.byte(src, cursor + 1, cursor + 3)
		if b1 == 0x72 and b2 == 0x75 and b3 == 0x65 then
			return cursor + 4, true
		end
	elseif b == 0x66 then -- 'f' — "false"
		local b1, b2, b3, b4 = string.byte(src, cursor + 1, cursor + 4)
		if b1 == 0x61 and b2 == 0x6C and b3 == 0x73 and b4 == 0x65 then
			return cursor + 5, false
		end
	elseif b == 0x6E then -- 'n' — "null"
		local b1, b2, b3 = string.byte(src, cursor + 1, cursor + 3)
		if b1 == 0x75 and b2 == 0x6C and b3 == 0x6C then
			return cursor + 4, json.null
		end
	elseif b == 0x2D or (b ~= nil and b >= 0x30 and b <= 0x39) then
		return parseNumber(src, cursor)
	end

	parseError(cursor, `Unexpected token '{string.sub(src, cursor, cursor)}'`)
end

function parseArray(src: string, cursor: number): (number, Array)
	local b: number?
	cursor, b = skipWs(src, cursor + 1) -- consume '[' + leading ws

	local out: Array = {}

	if b == 0x5D then -- ']'
		return cursor + 1, out
	end

	local i = 0
	while true do
		local value
		cursor, value = parseValueAt(src, cursor, b)
		i += 1
		out[i] = value

		cursor, b = skipWs(src, cursor)
		if b == 0x2C then -- ','
			cursor, b = skipWs(src, cursor + 1)
			if b == 0x5D then
				parseError(cursor, "Trailing comma in array")
			end
		elseif b == 0x5D then -- ']'
			return cursor + 1, out
		else
			parseError(cursor, "Expected ',' or ']' after array value")
		end
	end
end

function parseObject(src: string, cursor: number): (number, Object)
	local b: number?
	cursor, b = skipWs(src, cursor + 1) -- consume '{' + leading ws

	local out: Object = {}
	out[object_key] = true

	if b == 0x7D then -- '}'
		return cursor + 1, out
	end

	while true do
		if b ~= 0x22 then
			parseError(cursor, "Expected a string key")
		end
		local key
		cursor, key = parseString(src, cursor)

		cursor, b = skipWs(src, cursor)
		if b ~= 0x3A then
			parseError(cursor, "Expected ':' for key value pair")
		end
		cursor, b = skipWs(src, cursor + 1)

		local value
		cursor, value = parseValueAt(src, cursor, b)
		out[key] = value

		cursor, b = skipWs(src, cursor)
		if b == 0x2C then -- ','
			cursor, b = skipWs(src, cursor + 1)
		elseif b == 0x7D then -- '}'
			return cursor + 1, out
		else
			parseError(cursor, "Expected ',' or '}' after object value")
		end
	end
end

local function parseValue(src: string, cursor: number): (number, Value)
	local cur, b = skipWs(src, cursor)
	return parseValueAt(src, cur, b)
end

-- user-facing

--- Serializes `value` to a JSON string. If `prettyPrint` is `true`, the output is indented for readability.
function json.serialize(value: Value, prettyPrint: boolean?): string
	local out = {}
	if prettyPrint then
		serializePretty(out, 0, value, 0)
	else
		serializeCompact(out, 0, value)
	end
	return table.concat(out)
end

--- Parses a JSON string and returns the corresponding Luau value. Returns `json.null` for a JSON `null` literal; compare with `json.null` rather than `nil` to check for null.
function json.deserialize(src: string): (Array | Object | boolean | number | string)?
	local cursor, result = parseValue(src, 1)

	cursor = skipWs(src, cursor)
	if cursor <= #src then
		parseError(cursor, "Trailing characters after JSON value")
	end

	return result
end

--- Creates a JSON `Object` from `props`. Use this to distinguish an empty object `{}` from an empty array when serializing.
function json.object(props: { [string]: Value }): Object
	local res: Object = { [object_key] = true }

	for key, value in props do
		res[key] = value
	end

	return res
end

--- Returns `value` as an `Object` if it is a JSON object, or `nil` otherwise.
function json.asObject(value: Value): Object?
	if typeof(value) == "table" and value[object_key] then
		return value :: Object
	end

	return nil
end

--- Returns `value` as an `Array` if it is a JSON array, or `nil` otherwise.
function json.asArray(value: Value): Array?
	if typeof(value) == "table" and not value[object_key] then
		return value :: Array
	end

	return nil
end

return table.freeze(json)
