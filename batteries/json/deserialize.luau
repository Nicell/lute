type DeserializerState = {
	src: string,
	cursor: number,
}

local function deserializerError(state: DeserializerState, msg: string): never
	return error(`JSON error - {msg} around {state.cursor}`)
end

local function skipWhitespace(state: DeserializerState): boolean
	state.cursor = string.find(state.src, "%S", state.cursor) :: number

	if not state.cursor then
		return false
	end

	return true
end

local function currentByte(state: DeserializerState)
	return string.byte(state.src, state.cursor)
end

local function deserializeNumber(state: DeserializerState)
	-- first "segment"
	local nStart, nEnd = string.find(state.src, "^[%-%deE]*", state.cursor)

	if not nStart then
		-- i dont think this is possible
		deserializerError(state, "Could not match a number literal?")
	end

	if string.byte(state.src, nEnd :: number + 1) == string.byte(".") then -- decimal!
		local decStart, decEnd = string.find(state.src, "^[eE%-+%d]+", nEnd :: number + 2)

		if not decStart then
			deserializerError(state, "Trailing '.' in number value")
		end

		nEnd = decEnd
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
	state.cursor += 1

	local startPos = state.cursor

	if currentByte(state) == string.byte('"') then
		state.cursor += 1

		return ""
	end

	while state.cursor <= #state.src do
		if currentByte(state) == string.byte('"') then
			state.cursor += 1

			local source = string.sub(state.src, startPos, state.cursor - 2)

			source = string.gsub(
				source,
				"\\u([dD]83[dD])\\u(d[cC]%w%w)",
				function(high, low)
					return decodeSurrogatePair(high, low) or deserializerError(state, "Invalid unicode surrogate pair")
				end :: any
			)
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
			source = string.gsub(source, "\0", "\\")

			return source
		end

		if currentByte(state) == string.byte("\\") then
			state.cursor += 1
		end

		state.cursor += 1
	end

	-- error

	state.cursor = startPos

	return deserializerError(state, "Unterminated string")
end

local deserialize

local function deserializeArray(state: DeserializerState): Array
	state.cursor += 1

	local current: Array = {}

	local expectingValue = false
	while state.cursor < #state.src do
		skipWhitespace(state)

		if currentByte(state) == string.byte(",") then
			expectingValue = true
			state.cursor += 1
		end

		skipWhitespace(state)

		if currentByte(state) == string.byte("]") then
			break
		end

		table.insert(current, deserialize(state))

		expectingValue = false
	end

	if expectingValue then
		deserializerError(state, "Trailing comma")
	end

	if not skipWhitespace(state) or currentByte(state) ~= string.byte("]") then
		deserializerError(state, "Unterminated array")
	end

	state.cursor += 1

	return current
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

	if fourChars == "null" then
		state.cursor += 4
		return json.NULL
	elseif fourChars == "true" then
		state.cursor += 4
		return true
	elseif string.sub(state.src, state.cursor, state.cursor + 4) == "false" then
		state.cursor += 5
		return false
	elseif string.match(state.src, "^[%d%.]", state.cursor) then
		-- number
		return deserializeNumber(state)
	elseif string.byte(state.src, state.cursor) == string.byte('"') then
		return deserializeString(state)
	elseif string.byte(state.src, state.cursor) == string.byte("[") then
		return deserializeArray(state)
	elseif string.byte(state.src, state.cursor) == string.byte("{") then
		return deserializeObject(state)
	end

	return deserializerError(state, `Unexpected token '{string.sub(state.src, state.cursor, state.cursor)}'`)
end
json.deserialize = function(src: string)
	local state = {
		src = src,
		cursor = 0,
	}

	return deserialize(state)
end
