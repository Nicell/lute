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

type DeserializerState = {
	src: string,
	cursor: number,
}

local function deserializerError(state: DeserializerState, msg: string): never
	return error(`JSON error - {msg} around {state.cursor}`)
end

local function skipWhitespace(state: DeserializerState): boolean
	state.cursor = string.find(state.src, "[^ \n\r\t]", state.cursor) :: number

	if not state.cursor then
		return false
	end

	return true
end

local function currentByte(state: DeserializerState): number
	return (string.byte(state.src, state.cursor))
end

local function deserializeNumber(state: DeserializerState): number?
	-- Integer part
	local nStart, nEnd = string.find(state.src, "^-?[1-9]%d*", state.cursor)
	if not nStart then
		nStart, nEnd = string.find(state.src, "^-?0", state.cursor)
		if not nStart then
			deserializerError(state, "Malformed number (bad integer part)")
		end

		local nextChar = string.byte(state.src, nEnd :: number + 1)
		if nextChar and nextChar >= string.byte("0") and nextChar <= string.byte("9") then
			deserializerError(state, "Malformed number (leading zeros not allowed)")
		end
	end

	-- Decimal part
	local decStart, decEnd = string.find(state.src, "^%.%d+", nEnd :: number + 1)
	if decStart then
		nEnd = decEnd
	end

	-- Exponential part
	local expStart, expEnd = string.find(state.src, "^[eE][+-]?%d+", nEnd :: number + 1)
	if expStart then
		nEnd = expEnd
	end

	local num = tonumber(string.sub(state.src, nStart :: number, nEnd))
	if not num then
		deserializerError(state, "Malformed number value")
	end

	state.cursor = nEnd :: number + 1
	return num
end

local function decodeSurrogatePair(high, low): string?
	local highVal = tonumber(high, 16)
	local lowVal = tonumber(low, 16)
	if not highVal or not lowVal then
		return nil -- Invalid
	end
	-- Calculate the actual Unicode codepoint
	local codepoint = 0x10000 + ((highVal - 0xD800) * 0x400) + (lowVal - 0xDC00)
	return utf8.char(codepoint)
end

local function deserializeString(state: DeserializerState): string
	-- Must start with "
	if currentByte(state) ~= string.byte('"') then
		return deserializerError(state, "Expected string opening quote")
	end

	-- Skip opening quote "
	state.cursor += 1

	local startPos = state.cursor

	while state.cursor <= #state.src do
		local byte = currentByte(state)

		-- Check for unescaped control characters (0x00-0x1F)
		if byte < 0x20 then
			return deserializerError(state, "Unescaped control character in string")
		end

		if byte == string.byte('"') then
			state.cursor += 1
			local source = string.sub(state.src, startPos, state.cursor - 2)

			-- Validate escape sequences
			-- Remove valid \\ first
			local temp = string.gsub(source, "\\\\", "")

			-- Check for invalid single-char escapes
			if temp:match('\\[^bfnrt"\\/u]') then
				return deserializerError(state, "Invalid escape sequence")
			end

			-- Check that all \u escapes have exactly 4 hex digits
			if temp:match("\\u[^0-9a-fA-F]") or temp:match("\\u%x?%x?%x?$") then
				return deserializerError(state, "Incomplete or invalid \\u escape")
			end

			-- Handle all surrogate pairs
			source = string.gsub(
				source,
				"\\u([dD][89aAbB][0-9a-fA-F][0-9a-fA-F])\\u([dD][c-fC-F][0-9a-fA-F][0-9a-fA-F])",
				function(high, low)
					return decodeSurrogatePair(high, low) or deserializerError(state, "Invalid unicode surrogate pair")
				end :: any
			)

			-- Detect incomplete/invalid surrogates
			if source:match("\\u[dD][89aAbB][0-9a-fA-F][0-9a-fA-F]") then
				return deserializerError(state, "Incomplete surrogate pair")
			end

			-- Handle regular Unicode escapes
			source = string.gsub(source, "\\u(%x%x%x%x)", function(code)
				return utf8.char(tonumber(code, 16) :: number)
			end)

			source = string.gsub(source, "\\\\", "\0")
			source = string.gsub(source, "\\b", "\b")
			source = string.gsub(source, "\\f", "\f")
			source = string.gsub(source, "\\n", "\n")
			source = string.gsub(source, "\\r", "\r")
			source = string.gsub(source, "\\t", "\t")
			source = string.gsub(source, '\\"', '"')
			source = string.gsub(source, "\\/", "/")
			source = string.gsub(source, "\0", "\\")

			return source
		end

		if byte == string.byte("\\") then
			state.cursor += 1
		end

		state.cursor += 1
	end

	state.cursor = startPos
	return deserializerError(state, "Unterminated string")
end

local deserialize: (DeserializerState) -> (Array | Object | boolean | number | string)?

local function deserializeArray(state: DeserializerState): Array
	if currentByte(state) ~= string.byte("[") then
		return deserializerError(state, "Expected array opening '['")
	end

	state.cursor += 1
	skipWhitespace(state)

	local current: Array = {}
	local index = 1

	-- empty array
	if currentByte(state) == string.byte("]") then
		state.cursor += 1
		return current
	end

	local expectingValue = true

	while state.cursor <= #state.src do
		if not expectingValue then
			-- Expect a comma or closing bracket
			if currentByte(state) == string.byte(",") then
				expectingValue = true
				state.cursor += 1
				if not skipWhitespace(state) then
					return deserializerError(state, "Unterminated array")
				end
			elseif currentByte(state) == string.byte("]") then
				state.cursor += 1
				return current
			else
				return deserializerError(state, "Expected ',' or ']' after array value")
			end
		else
			-- Expect a value next
			if currentByte(state) == string.byte("]") then
				if index == 1 then
					-- empty array is ok
					state.cursor += 1
					return current
				else
					return deserializerError(state, "Trailing comma in array")
				end
			end

			table.insert(current, deserialize(state))
			index += 1
			expectingValue = false
		end

		if not skipWhitespace(state) then
			return deserializerError(state, "Unterminated array")
		end
	end

	return deserializerError(state, "Unterminated array")
end

local function deserializeObject(state: DeserializerState): Object
	state.cursor += 1

	local current = {}

	local expectingValue = false
	while state.cursor < #state.src do
		skipWhitespace(state)

		if currentByte(state) == string.byte("}") then
			break
		end

		skipWhitespace(state)

		if currentByte(state) ~= string.byte('"') then
			return deserializerError(state, "Expected a string key")
		end

		local key = deserializeString(state)

		skipWhitespace(state)

		if currentByte(state) ~= string.byte(":") then
			return deserializerError(state, "Expected ':' for key value pair")
		end

		state.cursor += 1

		local value = deserialize(state)

		current[key] = value

		if not skipWhitespace(state) then
			deserializerError(state, "Unterminated object")
		end

		skipWhitespace(state)

		if currentByte(state) == string.byte(",") then
			expectingValue = true
			state.cursor += 1
		else
			expectingValue = false
		end
	end

	if expectingValue then
		return deserializerError(state, "Trailing comma")
	end

	if not skipWhitespace(state) or currentByte(state) ~= string.byte("}") then
		deserializerError(state, "Unterminated object")
	end

	state.cursor += 1

	return current
end

deserialize = function(state: DeserializerState): Value
	skipWhitespace(state)

	local fourChars = string.sub(state.src, state.cursor, state.cursor + 3)

	if string.match(fourChars, "^null") then
		state.cursor += 4
		return json.null
	elseif string.match(fourChars, "^true") then
		state.cursor += 4
		return true
	elseif string.match(string.sub(state.src, state.cursor, state.cursor + 4), "^false") then
		state.cursor += 5
		return false
	elseif string.match(state.src, "^[%-%d]", state.cursor) then
		-- potential number
		return deserializeNumber(state)
	elseif string.match(state.src, '^"', state.cursor) then
		return deserializeString(state)
	elseif string.match(state.src, "^%[", state.cursor) then
		return deserializeArray(state)
	elseif string.match(state.src, "^{", state.cursor) then
		local obj = deserializeObject(state)
		obj[object_key] = true
		return obj
	end

	return deserializerError(state, `Unexpected token '{string.sub(state.src, state.cursor, state.cursor)}'`)
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
	local state = {
		src = src,
		cursor = 0,
	}

	local result = deserialize(state)

	-- After parsing a value, there must be no non-whitespace trailing characters.
	if skipWhitespace(state) then
		deserializerError(state, "Trailing characters after JSON value")
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
